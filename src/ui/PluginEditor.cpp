#include "ui/PluginEditor.h"

namespace dk::ui
{
namespace
{
juce::StringArray rateItems()
{
    juce::StringArray items;
    for (int i = 0; i < numRateDivisions; ++i)
        items.add (rateName ((RateDivision) i));
    return items;
}

juce::StringArray bandItems()
{
    juce::StringArray items;
    for (int i = 0; i < (int) DuckBand::NumBands; ++i)
        items.add (duckBandName ((DuckBand) i));
    return items;
}

juce::RangedAudioParameter& param (DucklingProcessor& p, const char* id)
{
    auto* parameter = p.getAPVTS().getParameter (id);
    jassert (parameter != nullptr);
    return *parameter;
}

//==============================================================================
// A plain panel with a section title and an optional dim hint on the right of the title row.
class TitledPanel : public juce::Component
{
public:
    void setTitleText (const juce::String& newTitle, const juce::String& newHint = {})
    {
        if (newTitle == title && newHint == hint)
            return;

        title = newTitle;
        hint = newHint;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        drawPanel (g, bounds);
        const auto header = bounds.reduced (16.0f, 0.0f).withHeight (36.0f).withTrimmedTop (2.0f);
        drawSectionTitle (g, title, header.toNearestInt());

        if (hint.isNotEmpty())
        {
            g.setFont (font (11.0f));
            g.setColour (Colours::textSubtle);
            g.drawText (hint, header, juce::Justification::centredRight, true);
        }
    }

private:
    juce::String title, hint;
};
} // namespace

//==============================================================================
struct DucklingEditor::Content final : public juce::Component
{
    explicit Content (DucklingProcessor& p)
        : processor (p),
          triggerPanel (param (p, ParamIDs::threshold)),
          triggerAttachment (param (p, ParamIDs::trigger), [this] (float v) { setMode (juce::roundToInt (v)); }, nullptr),
          rateAttachment (param (p, ParamIDs::rate), [this] (float v) { rate.setSelected (juce::roundToInt (v)); }, nullptr),
          curveAttachment (param (p, ParamIDs::curve), [this] (float v) { shapes.setSelected (juce::roundToInt (v)); }, nullptr),
          bandAttachment (param (p, ParamIDs::band), [this] (float v) { setBand (juce::roundToInt (v)); }, nullptr)
    {
        addAndMakeVisible (modeSwitch);
        modeSwitch.onChange = [this] (int index) { triggerAttachment.setValueAsCompleteGesture ((float) index); };

        help.setTooltip ("How to hook Duckling up in FL Studio");
        help.onClick = [this] { setHelpVisible (! overlay.isVisible()); };
        addAndMakeVisible (help);

        curve.onBrowse = [this] (int delta) {
            const int next = (shapes.getSelected() + delta + numCurveShapes) % numCurveShapes;
            curveAttachment.setValueAsCompleteGesture ((float) next);
        };
        addAndMakeVisible (curve);

        // Rate
        ratePanel.addAndMakeVisible (rate);
        rate.onChange = [this] (int index) { rateAttachment.setValueAsCompleteGesture ((float) index); };
        rate.setItemTooltips ({ "A 16th note", "An 8th note", "A quarter note (one beat)", "Half a bar", "One bar", "Two bars" });
        addAndMakeVisible (ratePanel);

        // Amount: depth knob, band, crossover
        depth.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        depth.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        depth.setPopupDisplayEnabled (true, true, this);
        depth.setTooltip ("Depth: how far each duck pulls the volume down. 100 % = all the way to silence.");
        depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getAPVTS(), ParamIDs::depth, depth);
        depth.setDoubleClickReturnValue (true, 100.0);
        depth.onValueChange = [this] { amountPanel.repaint(); };
        amountPanel.addAndMakeVisible (depth);

        band.onChange = [this] (int index) { bandAttachment.setValueAsCompleteGesture ((float) index); };
        band.setItemTooltips ({ "Duck the whole sound",
                                "Duck only the low end (below the crossover) - makes room for the kick, keeps the top steady",
                                "Duck only the top (above the crossover) - keeps the low end solid" });
        amountPanel.addAndMakeVisible (band);

        crossover.setSliderStyle (juce::Slider::LinearHorizontal);
        crossover.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        crossover.setPopupDisplayEnabled (true, true, this);
        crossover.setTooltip ("Crossover: where Lows end and Highs begin.");
        crossoverAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getAPVTS(), ParamIDs::crossover, crossover);
        crossover.setDoubleClickReturnValue (true, 150.0);
        crossover.onValueChange = [this] { amountPanel.repaint(); };
        amountPanel.addAndMakeVisible (crossover);
        amountPanel.labels = [this] (juce::Graphics& g) { paintAmountLabels (g); };
        addAndMakeVisible (amountPanel);

        shapes.onSelect = [this] (int index) { curveAttachment.setValueAsCompleteGesture ((float) index); };
        addAndMakeVisible (shapes);

        triggerPanel.onShowHelp = [this] { setHelpVisible (true); };
        addAndMakeVisible (triggerPanel);

        overlay.onClose = [this] { setHelpVisible (false); };
        addChildComponent (overlay);

        triggerAttachment.sendInitialUpdate();
        rateAttachment.sendInitialUpdate();
        curveAttachment.sendInitialUpdate();
        bandAttachment.sendInitialUpdate();

        neverTakeKeyboardFocus (*this);
    }

    //==============================================================================
    struct AmountPanel final : public TitledPanel
    {
        std::function<void (juce::Graphics&)> labels;
        void paintOverChildren (juce::Graphics& g) override { if (labels != nullptr) labels (g); }
    };

    //==============================================================================
    void setMode (int index)
    {
        mode = juce::jlimit (0, numTriggerModes - 1, index);
        modeSwitch.setSelected (mode);
        overlay.setHighlightedMode (mode);
        ratePanel.setTitleText (mode == (int) TriggerMode::Tempo ? "Rate" : "Length",
                                mode == (int) TriggerMode::Tempo ? "one duck every..." : "each duck lasts...");
        rate.setAccent (ModeSwitch::modeColour (mode));
        refresh();
    }

    void setBand (int index)
    {
        bandIndex = juce::jlimit (0, (int) DuckBand::NumBands - 1, index);
        band.setSelected (bandIndex);
        crossover.setEnabled (bandIndex != (int) DuckBand::Full);
        amountPanel.repaint();
    }

    void setHelpVisible (bool shouldShow)
    {
        overlay.setVisible (shouldShow);
        help.setToggleState (shouldShow, juce::dontSendNotification);

        if (shouldShow)
            overlay.toFront (false);
    }

    void paintAmountLabels (juce::Graphics& g)
    {
        const auto knob = depth.getBounds().toFloat();
        g.setFont (font (11.0f, 600));
        g.setColour (Colours::textSubtle);
        g.drawText ("Depth", knob.withY (knob.getBottom() + 2.0f).withHeight (14.0f), juce::Justification::centred, false);
        g.setFont (font (13.0f, 700));
        g.setColour (Colours::text);
        g.drawText (juce::String (juce::roundToInt (depth.getValue())) + "%", knob.withSizeKeepingCentre (44.0f, 16.0f),
                    juce::Justification::centred, false);

        const auto b = band.getBounds().toFloat();
        g.setFont (font (11.0f, 600));
        g.setColour (Colours::textSubtle);
        g.drawText ("Duck", b.withY (b.getY() - 17.0f).withHeight (14.0f), juce::Justification::centredLeft, false);

        const auto x = crossover.getBounds().toFloat();
        const bool enabled = crossover.isEnabled();
        const juce::String side = bandIndex == (int) DuckBand::Highs ? "above " : "below ";
        g.setColour (enabled ? Colours::textSubtle : Colours::textFaint);
        g.drawText ("Split", x.withY (x.getY() - 13.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
        g.setColour (enabled ? Colours::textDim : Colours::textFaint);
        g.drawText (enabled ? side + juce::String (juce::roundToInt (crossover.getValue())) + " Hz"
                            : juce::String (juce::roundToInt (crossover.getValue())) + " Hz",
                    x.withY (x.getY() - 13.0f).withHeight (14.0f), juce::Justification::centredRight, false);
    }

    //==============================================================================
    // Pulls the processor's live state (or the dev tool's override) into the panels.
    void refresh()
    {
        const auto display = displayOverride.has_value() ? *displayOverride : processor.getDisplayState();
        const auto& apvts = processor.getAPVTS();
        const auto rateIndex = juce::jlimit (0, numRateDivisions - 1, juce::roundToInt (apvts.getRawParameterValue (ParamIDs::rate)->load()));
        const juce::String rateText = rateName ((RateDivision) rateIndex);

        // Flash on every new duck cycle.
        if (! displayOverride.has_value())
        {
            if (display.triggerCount != lastTriggerCount || display.midiNoteCount != lastMidiCount)
                flash = 1.0f;
            else
                flash *= 0.86f;
        }

        lastTriggerCount = display.triggerCount;
        lastMidiCount = display.midiNoteCount;

        // The gain readout holds its lowest value for ~150 ms so it can be read.
        const auto now = juce::Time::getMillisecondCounter();

        if (display.gain <= heldGain || now - heldGainTime > 150)
        {
            heldGain = display.gain;
            heldGainTime = now;
        }

        CurveDisplay::State cs;
        cs.curve = (CurveShape) juce::jlimit (0, numCurveShapes - 1, juce::roundToInt (apvts.getRawParameterValue (ParamIDs::curve)->load()));
        cs.depth = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue (ParamIDs::depth)->load() * 0.01f);
        cs.mode = mode;
        cs.rateText = rateText;
        cs.phase = display.phase;
        cs.gain = displayOverride.has_value() ? display.gain : heldGain;

        switch ((TriggerMode) mode)
        {
            case TriggerMode::Tempo: cs.idleHint = "Press play in FL to see it move"; break;
            case TriggerMode::Audio: cs.idleHint = "Waiting for the kick..."; break;
            case TriggerMode::Midi:  cs.idleHint = "Waiting for notes..."; break;
            case TriggerMode::NumModes: break;
        }

        curve.setState (cs);

        TriggerPanel::State ts;
        ts.mode = mode;
        ts.display = display;
        ts.rateText = rateText;
        ts.flash = flash;
        triggerPanel.setState (ts);
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient::vertical (Colours::background.brighter (0.03f), 0.0f,
                                                           Colours::background.darker (0.08f), (float) getHeight()));
        g.fillAll();

        // Header: logo + wordmark ("Duck" + "ling" in mint, like Chord + bloom).
        drawLogo (g, juce::Rectangle<float> (18.0f, 13.0f, 34.0f, 34.0f));
        const auto wordFont = font (20.0f, 700);
        g.setFont (wordFont);
        g.setColour (Colours::text);
        g.drawText ("Duck", juce::Rectangle<float> (60.0f, 10.0f, 80.0f, 40.0f), juce::Justification::centredLeft, false);
        g.setColour (Colours::curve);
        g.drawText ("ling", juce::Rectangle<float> (60.0f + textWidth (wordFont, "Duck"), 10.0f, 60.0f, 40.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (Colours::outline.withAlpha (0.5f));
        g.fillRect (0.0f, 59.0f, (float) getWidth(), 1.0f);
    }

    void resized() override
    {
        modeSwitch.setBounds ((kDesignWidth - 372) / 2, 6, 372, 48);
        help.setBounds (kDesignWidth - 18 - 30, 15, 30, 30);

        const int left = 16, leftW = 452, gap = 10;
        const int right = left + leftW + gap, rightW = kDesignWidth - 16 - right;

        curve.setBounds (left, 70, leftW, 238);
        ratePanel.setBounds (right, 70, rightW, 104);
        rate.setBounds (16, 34, rightW - 32, 58);
        amountPanel.setBounds (right, 182, rightW, 126);
        depth.setBounds (12, 30, 70, 70);
        band.setBounds (96, 44, rightW - 96 - 14, 28);
        crossover.setBounds (92, 92, rightW - 92 - 10, 22);

        shapes.setBounds (left, 318, leftW, 148);
        triggerPanel.setBounds (right, 318, rightW, 148);

        overlay.setBounds (getLocalBounds());
    }

    DucklingProcessor& processor;

    ModeSwitch modeSwitch;
    IconButton help { "Help", IconButton::Icon::help };
    CurveDisplay curve;
    TitledPanel ratePanel;
    Segmented rate { rateItems(), 3 };
    AmountPanel amountPanel;
    LeftClickOnly<juce::Slider> depth;
    Segmented band { bandItems() };
    LeftClickOnly<juce::Slider> crossover;
    ShapePicker shapes;
    TriggerPanel triggerPanel;
    HelpOverlay overlay;

    juce::ParameterAttachment triggerAttachment, rateAttachment, curveAttachment, bandAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment, crossoverAttachment;

    int mode = 0, bandIndex = 0;
    std::optional<DucklingProcessor::DisplayState> displayOverride;
    float flash = 0.0f;
    uint32_t lastTriggerCount = 0, lastMidiCount = 0;
    float heldGain = 1.0f;
    juce::uint32 heldGainTime = 0;
};

//==============================================================================
DucklingEditor::DucklingEditor (DucklingProcessor& p) : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);
    content->setBounds (0, 0, kDesignWidth, kDesignHeight);

    // Size first, constrainer second: setResizeLimits() clamps the current (still empty) bounds, and resized() would
    // then save that clamped width as the user's choice.
    const double ratio = (double) kDesignWidth / (double) kDesignHeight;
    const int width = juce::jlimit (kDesignWidth * 3 / 4, kDesignWidth * 2, p.getEditorWidth());
    setSize (width, juce::roundToInt (width / ratio));
    setResizable (true, true);
    setResizeLimits (kDesignWidth * 3 / 4, kDesignHeight * 3 / 4, kDesignWidth * 2, kDesignHeight * 2);
    getConstrainer()->setFixedAspectRatio (ratio);

    content->refresh();
    startTimerHz (60);
}

DucklingEditor::~DucklingEditor()
{
    stopTimer();
    content.reset();
    setLookAndFeel (nullptr);
}

void DucklingEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void DucklingEditor::resized()
{
    const float scale = (float) getWidth() / (float) kDesignWidth;
    content->setTransform (juce::AffineTransform::scale (scale));

    if (getWidth() > 0)
        processor.setEditorWidth (getWidth());
}

void DucklingEditor::timerCallback()
{
    content->refresh();
}

void DucklingEditor::setHelpVisible (bool shouldShow)
{
    content->setHelpVisible (shouldShow);
}

void DucklingEditor::setDisplayOverride (std::optional<DucklingProcessor::DisplayState> state, float flash)
{
    content->displayOverride = std::move (state);
    content->flash = flash;
    content->refresh();
}

void DucklingEditor::refreshNow()
{
    content->refresh();
}
} // namespace dk::ui
