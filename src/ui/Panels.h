#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/Widgets.h"

// The editor's panels. Each one only draws the state it is given and reports gestures through callbacks or a
// juce::ParameterAttachment; the editor owns the timer that feeds them.
namespace dk::ui
{
// Path of a curve's gain (1 at the top of `area`, 0 at the bottom) over one cycle, `depth` 0..1.
juce::Path gainCurvePath (CurveShape, float depth, juce::Rectangle<float> area, int points = 160);

//==============================================================================
// The big curve view: the selected shape at the current depth, the live playhead and the gain readout. The mouse
// wheel browses the shapes.
class CurveDisplay final : public juce::Component
{
public:
    struct State
    {
        CurveShape curve = CurveShape::Classic;
        float depth = 1.0f;
        int mode = 0;                 // TriggerMode
        juce::String rateText;        // "1/4", "1 bar"
        float phase = -1.0f;          // playhead, -1 hidden
        float gain = 1.0f;            // readout (linear)
        juce::String idleHint;        // shown when there is no playhead

        bool operator== (const State& o) const
        {
            return curve == o.curve && depth == o.depth && mode == o.mode && rateText == o.rateText
                   && phase == o.phase && gain == o.gain && idleHint == o.idleHint;
        }
    };

    CurveDisplay();

    void setState (const State&);
    std::function<void (int delta)> onBrowse;   // +1 / -1 from the mouse wheel

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    State state;
    float wheelAccumulator = 0.0f;
};

//==============================================================================
// Grid of curve thumbnails (6 x 2). Left click picks one; hover shows the description.
class ShapePicker final : public juce::Component, public juce::TooltipClient
{
public:
    ShapePicker();

    void setSelected (int index);   // no callback
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onSelect;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static constexpr int kColumns = 6;

private:
    juce::Rectangle<float> tileBounds (int index) const;
    int indexAt (juce::Point<float>) const;

    int selected = 0, hovered = -1;
};

//==============================================================================
// Horizontal sidechain meter with a draggable threshold line (Audio mode). Double-click resets the threshold.
class ThresholdMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ThresholdMeter (juce::RangedAudioParameter& thresholdParameter);
    ~ThresholdMeter() override;

    void setLevel (float levelDb, bool flash);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    static constexpr float kMinDb = -60.0f;

private:
    juce::Rectangle<float> barArea() const;
    float xForDb (float db) const;
    float dbForX (float x) const;

    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    float thresholdDb = -24.0f;
    float levelDb = -100.0f;
    float flashAmount = 0.0f;
    bool dragging = false;
};

//==============================================================================
// The bottom-right panel: what drives the ducking in the current mode (tempo / sidechain meter / MIDI notes) and
// whether it is working.
class TriggerPanel final : public juce::Component
{
public:
    explicit TriggerPanel (juce::RangedAudioParameter& thresholdParameter);

    struct State
    {
        int mode = 0;
        DucklingProcessor::DisplayState display;
        juce::String rateText;
        float flash = 0.0f;   // 0..1, decays after every trigger
    };

    void setState (const State&);
    std::function<void()> onShowHelp;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> textArea() const;
    juce::String bodyText() const;

    State state;
    ThresholdMeter meter;
};

//==============================================================================
// "How do I hook this up in FL Studio?" - three columns of steps, one per trigger mode. Click anywhere to close.
class HelpOverlay final : public juce::Component
{
public:
    HelpOverlay();

    std::function<void()> onClose;
    void setHighlightedMode (int mode) { highlighted = mode; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cardArea() const;

    IconButton close { "Close help", IconButton::Icon::close };
    int highlighted = 0;
};
} // namespace dk::ui
