#include "ui/Panels.h"

namespace dk::ui
{
namespace
{
const juce::String kDash = juce::String::fromUTF8 ("\xe2\x80\x94");   // em dash

// The plot frame shared by the big display and the thumbnails: dark inset with a soft outline.
void drawPlotBackground (juce::Graphics& g, juce::Rectangle<float> area, float radius)
{
    g.setGradientFill (juce::ColourGradient::vertical (Colours::background.darker (0.18f), area.getY(),
                                                       Colours::background.darker (0.05f), area.getBottom()));
    g.fillRoundedRectangle (area, radius);
    g.setColour (Colours::outline.withAlpha (0.55f));
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
}

float yForGain (juce::Rectangle<float> area, float gain)
{
    return area.getBottom() - juce::jlimit (0.0f, 1.0f, gain) * area.getHeight();
}

// A small status pill: coloured dot + text, right-aligned in `area`. Returns its bounds.
juce::Rectangle<float> drawStatusPill (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                                       juce::Colour colour, float glow)
{
    const auto f = font (11.0f, 600);
    const float w = textWidth (f, text) + 28.0f;
    const auto pill = area.withLeft (area.getRight() - w).withSizeKeepingCentre (w, 20.0f);

    g.setColour (blend (Colours::panelRaised, colour, 0.12f + 0.25f * glow));
    g.fillRoundedRectangle (pill, 10.0f);
    g.setColour (colour.withAlpha (0.35f + 0.5f * glow));
    g.drawRoundedRectangle (pill.reduced (0.5f), 10.0f, 1.0f);

    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ pill.getX() + 12.0f, pill.getCentreY() });

    if (glow > 0.01f)
    {
        g.setColour (colour.withAlpha (0.35f * glow));
        g.fillEllipse (dot.expanded (3.0f * glow));
    }

    g.setColour (colour);
    g.fillEllipse (dot);
    g.setFont (f);
    g.setColour (Colours::text.withAlpha (0.92f));
    g.drawText (text, pill.withTrimmedLeft (20.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, false);
    return pill;
}
} // namespace

juce::Path gainCurvePath (CurveShape shape, float depth, juce::Rectangle<float> area, int points)
{
    juce::Path p;
    points = juce::jmax (2, points);

    for (int i = 0; i <= points; ++i)
    {
        const float phase = (float) i / (float) points;
        const float gain = 1.0f - depth * duckAmount (shape, phase);
        const juce::Point<float> pt (area.getX() + phase * area.getWidth(), yForGain (area, gain));

        if (i == 0)
        {
            // Start at full volume (the end of the previous cycle) and drop straight down at the trigger.
            p.startNewSubPath (area.getX(), area.getY());
            p.lineTo (pt);
        }
        else
        {
            p.lineTo (pt);
        }
    }

    return p;
}

//==============================================================================
CurveDisplay::CurveDisplay()
{
    setTitle ("Curve");
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void CurveDisplay::setState (const State& newState)
{
    if (newState == state)
        return;

    state = newState;
    repaint();
}

void CurveDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    wheelAccumulator += wheel.deltaY;

    if (std::abs (wheelAccumulator) >= 0.12f)
    {
        if (onBrowse != nullptr)
            onBrowse (wheelAccumulator > 0.0f ? -1 : 1);

        wheelAccumulator = 0.0f;
    }
}

void CurveDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawPanel (g, bounds);

    const auto modeColour = ModeSwitch::modeColour (state.mode);

    // Header: title, curve name, gain readout.
    auto header = bounds.reduced (16.0f, 0.0f).withHeight (38.0f).withTrimmedTop (4.0f);
    drawSectionTitle (g, "Curve", header.toNearestInt());
    const float titleW = textWidth (font (10.5f, 600).withExtraKerningFactor (0.09f), "CURVE") + 12.0f;
    g.setFont (font (14.5f, 600));
    g.setColour (Colours::text);
    g.drawText (curveName (state.curve), header.withTrimmedLeft (titleW), juce::Justification::centredLeft, false);

    {
        const auto readout = fmt::gainDb (state.gain);
        const auto valueFont = font (15.0f, 700);
        const float valueW = textWidth (valueFont, readout) + 2.0f;
        g.setFont (valueFont);
        g.setColour (state.gain < 0.999f ? Colours::curve.brighter (0.15f) : Colours::textDim);
        g.drawText (readout, header.removeFromRight (valueW), juce::Justification::centredRight, false);
        header.removeFromRight (8.0f);
        drawSectionTitle (g, "Gain", header.toNearestInt(), juce::Justification::centredRight);
    }

    // Plot
    const auto frame = bounds.reduced (14.0f).withTrimmedTop (30.0f).withTrimmedBottom (18.0f);
    drawPlotBackground (g, frame, 7.0f);
    const auto plot = frame.reduced (12.0f, 10.0f);

    // Grid: quarters of the cycle; -6 / -12 / -18 dB lines (linear gain 0.5 / 0.25 / 0.125).
    g.setColour (Colours::outline.withAlpha (0.45f));

    for (int i = 1; i < 4; ++i)
    {
        const float x = std::round (plot.getX() + plot.getWidth() * (float) i / 4.0f) + 0.5f;
        g.drawVerticalLine ((int) x, frame.getY() + 1.0f, frame.getBottom() - 1.0f);
    }

    const float gridGains[] = { 0.5f, 0.25f };
    const char* const gridLabels[] = { "-6 dB", "-12" };
    g.setFont (font (9.5f, 600));

    for (int i = 0; i < 2; ++i)
    {
        const float y = std::round (yForGain (plot, gridGains[i])) + 0.5f;
        g.setColour (Colours::outline.withAlpha (0.35f));
        g.drawHorizontalLine ((int) y, frame.getX() + 1.0f, frame.getRight() - 1.0f);
        g.setColour (Colours::textFaint);
        g.drawText (gridLabels[i], juce::Rectangle<float> (frame.getRight() - 40.0f, y - 12.0f, 36.0f, 11.0f),
                    juce::Justification::centredRight, false);
    }

    // The ducked-away part (above the curve) in a faint tint, the remaining volume (below) in mint.
    const auto curvePath = gainCurvePath (state.curve, state.depth, plot);

    {
        juce::Path below (curvePath);
        below.lineTo (plot.getRight(), plot.getBottom());
        below.lineTo (plot.getX(), plot.getBottom());
        below.closeSubPath();
        g.setGradientFill (juce::ColourGradient::vertical (Colours::curve.withAlpha (0.30f), plot.getY(),
                                                           Colours::curve.withAlpha (0.04f), plot.getBottom()));
        g.fillPath (below);

        juce::Path above (curvePath);
        above.lineTo (plot.getRight(), plot.getY());
        above.closeSubPath();
        g.setColour (Colours::trigger.withAlpha (0.07f));
        g.fillPath (above);
    }

    if (state.depth < 0.995f)
    {
        // The full-depth shape, dashed, so the depth's effect is visible.
        juce::Path dashed;
        const float dashes[] = { 3.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, gainCurvePath (state.curve, 1.0f, plot), dashes, 2);
        g.setColour (Colours::textFaint.withAlpha (0.6f));
        g.fillPath (dashed);
    }

    g.setColour (Colours::curve);
    g.strokePath (curvePath, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Playhead with a short glowing trail along the curve.
    if (state.phase >= 0.0f)
    {
        const float phase = juce::jlimit (0.0f, 1.0f, state.phase);
        const float x = plot.getX() + phase * plot.getWidth();
        const float gain = 1.0f - state.depth * duckAmount (state.curve, phase);
        const juce::Point<float> dot (x, yForGain (plot, gain));

        g.setGradientFill (juce::ColourGradient::vertical (modeColour.withAlpha (0.0f), frame.getY(),
                                                           modeColour.withAlpha (0.45f), frame.getBottom()));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, frame.getY() + 1.0f, 1.5f, frame.getHeight() - 2.0f));

        const float trailStart = juce::jmax (0.0f, phase - 0.14f);
        const int steps = 24;

        for (int i = 0; i < steps; ++i)
        {
            const float p0 = trailStart + (phase - trailStart) * (float) i / (float) steps;
            const float p1 = trailStart + (phase - trailStart) * (float) (i + 1) / (float) steps;
            const juce::Point<float> a (plot.getX() + p0 * plot.getWidth(),
                                        yForGain (plot, 1.0f - state.depth * duckAmount (state.curve, p0)));
            const juce::Point<float> b (plot.getX() + p1 * plot.getWidth(),
                                        yForGain (plot, 1.0f - state.depth * duckAmount (state.curve, p1)));
            g.setColour (Colours::curve.brighter (0.4f).withAlpha (0.9f * (float) (i + 1) / (float) steps));
            g.drawLine ({ a, b }, 3.6f);
        }

        g.setColour (modeColour.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (dot));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot));
        g.setColour (modeColour);
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot), 1.6f);
    }
    else if (state.idleHint.isNotEmpty())
    {
        const auto hintFont = font (12.0f, 600);
        const float w = textWidth (hintFont, state.idleHint) + 26.0f;
        const auto pill = juce::Rectangle<float> (w, 26.0f).withCentre ({ plot.getCentreX() + plot.getWidth() * 0.12f,
                                                                           plot.getY() + plot.getHeight() * 0.62f });
        g.setColour (Colours::background.withAlpha (0.85f));
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (pill.reduced (0.5f), 13.0f, 1.0f);
        g.setFont (hintFont);
        g.setColour (Colours::textDim);
        g.drawText (state.idleHint, pill, juce::Justification::centred, false);
    }

    // X axis: the trigger on the left, the next one (one Rate later) on the right.
    const auto axis = juce::Rectangle<float> (plot.getX(), frame.getBottom() + 2.0f, plot.getWidth(), 14.0f);
    g.setFont (font (10.5f, 600));
    g.setColour (Colours::textSubtle);
    const juce::String startLabel = state.mode == (int) TriggerMode::Tempo ? "beat" : (state.mode == (int) TriggerMode::Audio ? "kick" : "note");
    g.drawText (startLabel, axis, juce::Justification::centredLeft, false);
    g.drawText (state.rateText + " later", axis, juce::Justification::centredRight, false);
    g.setColour (Colours::textFaint);
    g.drawText ("scroll to change the shape", axis, juce::Justification::centred, false);
}

//==============================================================================
ShapePicker::ShapePicker()
{
    setTitle ("Shapes");
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void ShapePicker::setSelected (int index)
{
    index = juce::jlimit (0, numCurveShapes - 1, index);

    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

juce::Rectangle<float> ShapePicker::tileBounds (int index) const
{
    const auto area = getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (12.0f);
    const int rows = (numCurveShapes + kColumns - 1) / kColumns;
    const float gap = 6.0f;
    const float w = (area.getWidth() - gap * (float) (kColumns - 1)) / (float) kColumns;
    const float h = (area.getHeight() - gap * (float) (rows - 1)) / (float) rows;
    return { area.getX() + (float) (index % kColumns) * (w + gap), area.getY() + (float) (index / kColumns) * (h + gap), w, h };
}

int ShapePicker::indexAt (juce::Point<float> p) const
{
    for (int i = 0; i < numCurveShapes; ++i)
        if (tileBounds (i).contains (p))
            return i;

    return -1;
}

void ShapePicker::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawPanel (g, bounds);
    drawSectionTitle (g, "Shape", bounds.reduced (16.0f, 0.0f).withHeight (36.0f).withTrimmedTop (2.0f).toNearestInt());

    for (int i = 0; i < numCurveShapes; ++i)
    {
        const auto tile = tileBounds (i);
        const bool isSelected = i == selected;
        const bool isHovered = i == hovered;
        const float radius = 7.0f;

        if (isSelected)
        {
            juce::Path shape;
            shape.addRoundedRectangle (tile, radius);
            juce::DropShadow (Colours::curve.withAlpha (0.30f), 10, {}).drawForPath (g, shape);
        }

        auto fill = isSelected ? blend (Colours::panelRaised, Colours::curve, 0.16f)
                               : (isHovered ? Colours::panelHover : Colours::panelRaised);
        g.setGradientFill (juce::ColourGradient::vertical (fill.brighter (0.04f), tile.getY(), fill.darker (0.06f), tile.getBottom()));
        g.fillRoundedRectangle (tile, radius);
        g.setColour (isSelected ? Colours::curve.withAlpha (0.85f) : Colours::outline.brighter (isHovered ? 0.2f : 0.0f).withAlpha (0.8f));
        g.drawRoundedRectangle (tile.reduced (0.5f), radius, isSelected ? 1.3f : 1.0f);

        const auto label = tile.withTrimmedTop (tile.getHeight() - 17.0f);
        const auto plot = tile.reduced (9.0f, 0.0f).withTrimmedTop (8.0f).withBottom (label.getY() - 1.0f);
        const auto path = gainCurvePath ((CurveShape) i, 1.0f, plot, 48);

        juce::Path below (path);
        below.lineTo (plot.getRight(), plot.getBottom());
        below.lineTo (plot.getX(), plot.getBottom());
        below.closeSubPath();
        g.setColour ((isSelected ? Colours::curve : Colours::textDim).withAlpha (isSelected ? 0.22f : 0.10f));
        g.fillPath (below);
        g.setColour (isSelected ? Colours::curve : (isHovered ? Colours::text.withAlpha (0.85f) : Colours::textDim));
        g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (font (11.0f, isSelected ? 600 : 400));
        g.setColour (isSelected ? Colours::text : Colours::textDim.brighter (isHovered ? 0.3f : 0.0f));
        g.drawText (curveName ((CurveShape) i), label.withTrimmedBottom (3.0f), juce::Justification::centred, false);
    }
}

void ShapePicker::mouseDown (const juce::MouseEvent& e)
{
    if (! isLeftClick (e))
        return;

    const int index = indexAt (e.position);

    if (index >= 0 && index != selected)
    {
        setSelected (index);

        if (onSelect != nullptr)
            onSelect (index);
    }
}

void ShapePicker::mouseMove (const juce::MouseEvent& e)
{
    const int index = indexAt (e.position);

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void ShapePicker::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

juce::String ShapePicker::getTooltip()
{
    if (hovered < 0)
        return {};

    return juce::String (curveName ((CurveShape) hovered)) + ": " + curveDescription ((CurveShape) hovered);
}

//==============================================================================
ThresholdMeter::ThresholdMeter (juce::RangedAudioParameter& p)
    : parameter (p),
      attachment (p, [this] (float value) { thresholdDb = value; repaint(); }, nullptr)
{
    setTitle ("Threshold");
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setTooltip ("Drag the line to set the threshold: a duck starts when the sidechain gets louder than it. "
                "Double-click to reset.");
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    attachment.sendInitialUpdate();
}

ThresholdMeter::~ThresholdMeter()
{
    // The editor can close mid-drag: never leave the host with an open automation gesture.
    if (dragging)
        attachment.endGesture();
}

void ThresholdMeter::setLevel (float newLevelDb, bool flash)
{
    flash = flash || flashAmount > 0.01f;
    const float newFlash = flash ? juce::jmax (flashAmount * 0.85f, 0.0f) : 0.0f;

    if (std::abs (newLevelDb - levelDb) < 0.05f && std::abs (newFlash - flashAmount) < 0.005f)
        return;

    levelDb = newLevelDb;
    flashAmount = newFlash;
    repaint();
}

juce::Rectangle<float> ThresholdMeter::barArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (16.0f).reduced (1.0f, 0.0f).withHeight (14.0f);
}

float ThresholdMeter::xForDb (float db) const
{
    const auto bar = barArea();
    return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db - kMinDb) / -kMinDb);
}

float ThresholdMeter::dbForX (float x) const
{
    const auto bar = barArea();
    return juce::jlimit (kMinDb, 0.0f, kMinDb + (x - bar.getX()) / juce::jmax (1.0f, bar.getWidth()) * -kMinDb);
}

void ThresholdMeter::paint (juce::Graphics& g)
{
    const auto bar = barArea();
    const bool hover = isMouseOverOrDragging();

    // Labels
    g.setFont (font (10.5f, 600));
    g.setColour (Colours::textSubtle);
    g.drawText ("Threshold", getLocalBounds().toFloat().withHeight (13.0f), juce::Justification::centredLeft, false);
    g.setColour (hover ? Colours::text : Colours::textDim);
    g.drawText (fmt::number (thresholdDb, 1) + " dB", getLocalBounds().toFloat().withHeight (13.0f),
                juce::Justification::centredRight, false);

    // Track
    g.setColour (Colours::background.darker (0.15f));
    g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (Colours::outline.withAlpha (0.7f));
    g.drawRoundedRectangle (bar.reduced (0.5f), 4.0f, 1.0f);

    // Level: lavender up to the threshold, brighter above it.
    const float levelX = xForDb (levelDb);
    const float thresholdX = xForDb (thresholdDb);

    if (levelX > bar.getX() + 1.0f)
    {
        auto fill = bar.reduced (2.0f).withRight (levelX);
        g.setColour (Colours::trigger.withAlpha (0.55f));
        g.fillRoundedRectangle (fill.withRight (juce::jmin (levelX, thresholdX)), 2.5f);

        if (levelX > thresholdX)
        {
            g.setColour (Colours::trigger.brighter (0.35f));
            g.fillRoundedRectangle (fill.withLeft (thresholdX), 2.5f);
        }
    }

    if (flashAmount > 0.01f)
    {
        g.setColour (Colours::trigger.withAlpha (0.35f * flashAmount));
        g.drawRoundedRectangle (bar.expanded (1.5f), 5.0f, 2.0f);
    }

    // Threshold handle
    const auto handle = juce::Rectangle<float> (hover ? 6.0f : 4.0f, bar.getHeight() + 8.0f).withCentre ({ thresholdX, bar.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (handle.translated (0.0f, 1.0f), 2.0f);
    g.setColour (hover ? Colours::curve.brighter (0.3f) : Colours::curve);
    g.fillRoundedRectangle (handle, 2.0f);

    // Scale
    g.setFont (font (9.0f, 600));
    g.setColour (Colours::textFaint);

    for (const float db : { -60.0f, -40.0f, -20.0f, 0.0f })
    {
        const float x = xForDb (db);
        const auto r = juce::Rectangle<float> (x - 15.0f, bar.getBottom() + 4.0f, 30.0f, 10.0f);
        const auto just = db <= kMinDb ? juce::Justification::centredLeft : (db >= 0.0f ? juce::Justification::centredRight : juce::Justification::centred);
        g.drawText (fmt::number (db, 0), db <= kMinDb ? r.withX (x) : (db >= 0.0f ? r.withRightX (x) : r), just, false);
    }
}

void ThresholdMeter::mouseDown (const juce::MouseEvent& e)
{
    if (! isLeftClick (e))
        return;

    dragging = true;
    attachment.beginGesture();
    attachment.setValueAsPartOfGesture (dbForX (e.position.x));
}

void ThresholdMeter::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        attachment.setValueAsPartOfGesture (dbForX (e.position.x));
}

void ThresholdMeter::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }
}

void ThresholdMeter::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isLeftClick (e))
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (parameter.getDefaultValue()));
}

//==============================================================================
TriggerPanel::TriggerPanel (juce::RangedAudioParameter& thresholdParameter) : meter (thresholdParameter)
{
    addChildComponent (meter);
}

void TriggerPanel::setState (const State& newState)
{
    const bool modeChanged = newState.mode != state.mode;
    state = newState;
    meter.setVisible (state.mode == (int) TriggerMode::Audio);

    if (state.mode == (int) TriggerMode::Audio)
        meter.setLevel (state.display.sidechainDb, state.flash > 0.5f);

    if (modeChanged)
        resized();

    repaint();
}

void TriggerPanel::resized()
{
    meter.setBounds (getLocalBounds().reduced (16, 0).withTrimmedTop (44).withHeight (44));
}

juce::Rectangle<float> TriggerPanel::textArea() const
{
    const float top = state.mode == (int) TriggerMode::Audio ? 94.0f : 78.0f;
    return getLocalBounds().toFloat().reduced (16.0f, 0.0f).withTrimmedTop (top).withTrimmedBottom (10.0f);
}

juce::String TriggerPanel::bodyText() const
{
    const auto& d = state.display;

    switch ((TriggerMode) state.mode)
    {
        case TriggerMode::Tempo:
            if (d.processing && ! d.hostHasPosition)
                return "No song position from the host, so Duckling keeps its own beat at **" + fmt::number ((float) d.bpm, 0) + " BPM**.";

            return "Ducks on every **" + state.rateText + "** in time with your project. Nothing to route " + kDash + " just press play.";

        case TriggerMode::Audio:
            if (d.processing && d.sidechainChannels > 0 && d.sidechainHeard)
                return "Ducks each time the sidechain crosses the line. Drag the line just under your kick's peaks.";

            return "**No sidechain signal yet.** Route your kick to Duckling " + kDash + " click here for the steps.";

        case TriggerMode::Midi:
            if (d.midiNoteCount > 0)
                return "Every note Duckling receives starts a duck " + kDash + " draw them where your kicks are.";

            return "**No notes yet.** Send notes from a MIDI Out channel " + kDash + " click here for the steps.";

        case TriggerMode::NumModes:
            break;
    }

    return {};
}

void TriggerPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawPanel (g, bounds);

    const auto& d = state.display;
    const auto mode = (TriggerMode) state.mode;
    const auto colour = ModeSwitch::modeColour (state.mode);
    const auto header = bounds.reduced (16.0f, 0.0f).withHeight (38.0f).withTrimmedTop (2.0f);

    const char* title = mode == TriggerMode::Tempo ? "Tempo sync" : (mode == TriggerMode::Audio ? "Sidechain" : "MIDI trigger");
    drawSectionTitle (g, title, header.toNearestInt());

    switch (mode)
    {
        case TriggerMode::Tempo:
        {
            const bool running = d.processing && (d.hostPlaying || ! d.hostHasPosition);
            drawStatusPill (g, header, running ? "Playing" : "Stopped", running ? colour : Colours::textFaint, running ? state.flash : 0.0f);

            const auto big = bounds.reduced (16.0f, 0.0f).withTrimmedTop (38.0f).withHeight (34.0f);
            const auto bpmText = d.processing ? fmt::number ((float) d.bpm, d.bpm == std::round (d.bpm) ? 0 : 1) : juce::String ("--");
            const auto bigFont = font (24.0f, 700);
            g.setFont (bigFont);
            g.setColour (d.processing ? Colours::text : Colours::textFaint);
            g.drawText (bpmText, big, juce::Justification::centredLeft, false);
            g.setFont (font (12.0f, 600));
            g.setColour (Colours::textDim);
            g.drawText ("BPM", big.withTrimmedLeft (textWidth (bigFont, bpmText) + 6.0f).translated (0.0f, 3.0f),
                        juce::Justification::centredLeft, false);
            break;
        }

        case TriggerMode::Audio:
        {
            const bool heard = d.processing && d.sidechainChannels > 0 && d.sidechainHeard;
            drawStatusPill (g, header, ! heard ? "No signal" : (state.flash > 0.3f ? "Duck!" : "Listening"),
                            heard ? colour : Colours::warning, heard ? state.flash : 0.0f);
            break;
        }

        case TriggerMode::Midi:
        {
            const auto count = d.midiNoteCount;
            const juce::String text = count == 0 ? juce::String ("No notes") : juce::String (count) + (count == 1 ? " note" : " notes");
            drawStatusPill (g, header, text, count > 0 ? colour : Colours::warning, count > 0 ? state.flash : 0.0f);

            // A row of eight "LEDs" that light up in turn on every note.
            const auto row = bounds.reduced (16.0f, 0.0f).withTrimmedTop (44.0f).withHeight (24.0f);
            const int lit = (int) (count % 8u);

            for (int i = 0; i < 8; ++i)
            {
                const float w = (row.getWidth() - 7.0f * 6.0f) / 8.0f;
                const auto led = juce::Rectangle<float> (row.getX() + (float) i * (w + 6.0f), row.getY() + 6.0f, w, 12.0f);
                const bool on = count > 0 && i == (lit + 7) % 8;
                g.setColour (on ? colour.withAlpha (0.35f + 0.65f * juce::jmax (0.3f, state.flash)) : Colours::panelRaised);
                g.fillRoundedRectangle (led, 4.0f);
                g.setColour (on ? colour : Colours::outline.withAlpha (0.8f));
                g.drawRoundedRectangle (led.reduced (0.5f), 4.0f, 1.0f);
            }
            break;
        }

        case TriggerMode::NumModes:
            break;
    }

    juce::TextLayout layout;
    layout.createLayout (richText (bodyText(), 12.0f, Colours::textDim, Colours::text, 2.5f), textArea().getWidth());
    layout.draw (g, textArea().withHeight (layout.getHeight()));
}

void TriggerPanel::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (textArea().contains (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void TriggerPanel::mouseUp (const juce::MouseEvent& e)
{
    if (isLeftClick (e) && textArea().contains (e.position) && onShowHelp != nullptr)
        onShowHelp();
}

//==============================================================================
namespace
{
struct HelpColumn
{
    const char* title;
    const char* subtitle;
    const char* steps[4];
};

const HelpColumn helpColumns[] = {
    { "TEMPO", "easiest - no routing",
      { "Add Duckling to the **mixer track you want to pump** (bass, chords, pads...).",
        "Pick a **shape**, then a **rate**: 1/4 ducks on every beat, like a four-on-the-floor kick.",
        "Press **play** in FL. Done.",
        "Tip: turn **Depth** down for a subtler pump, or duck only the **Lows** to make room for the kick." } },
    { "AUDIO", "follows your real kick",
      { "Add Duckling to the track you want to duck.",
        "In the Mixer, select your **kick's track**, right-click the arrow under Duckling's track and choose "
        "**Sidechain to this track**.",
        "Open Duckling's **wrapper settings** (gear icon at the top of its window) > **Processing** tab, and pick the "
        "kick's track as the **sidechain input**.",
        "Play. The status says **Listening** - drag the threshold line just under the kick's peaks." } },
    { "MIDI", "draw your own pattern",
      { "In the Channel rack add **(+) > MIDI Out** and set its **Port** (e.g. 1).",
        "Open Duckling's **wrapper settings** (gear icon) > **Settings** tab, and set the MIDI **Input port** to the "
        "same number.",
        "Draw notes in the MIDI Out channel's pattern - **every note is a duck**. Pitch doesn't matter.",
        "Tip: copy your kick's notes into it." } },
};
} // namespace

HelpOverlay::HelpOverlay()
{
    setInterceptsMouseClicks (true, true);
    close.roundBackground = true;
    close.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (close);
}

juce::Rectangle<float> HelpOverlay::cardArea() const
{
    return getLocalBounds().toFloat().reduced (24.0f, 20.0f);
}

void HelpOverlay::resized()
{
    const auto card = cardArea().toNearestInt();
    close.setBounds (card.getRight() - 40, card.getY() + 12, 28, 28);
}

void HelpOverlay::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background.withAlpha (0.78f));

    const auto card = cardArea();
    {
        juce::Path shape;
        shape.addRoundedRectangle (card, 12.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 24, { 0, 6 }).drawForPath (g, shape);
    }
    g.setGradientFill (juce::ColourGradient::vertical (Colours::panelRaised, card.getY(), Colours::panel, card.getBottom()));
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);

    auto content = card.reduced (22.0f, 18.0f);
    auto titleRow = content.removeFromTop (28.0f);
    drawLogo (g, titleRow.removeFromLeft (26.0f).withSizeKeepingCentre (24.0f, 24.0f));
    titleRow.removeFromLeft (10.0f);
    g.setFont (font (17.0f, 700));
    g.setColour (Colours::text);
    g.drawText ("Hooking Duckling up in FL Studio", titleRow, juce::Justification::centredLeft, false);

    content.removeFromTop (6.0f);
    g.setFont (font (12.0f));
    g.setColour (Colours::textDim);
    g.drawText ("Pick a trigger at the top. Tempo needs no routing at all - start there.", content.removeFromTop (18.0f),
                juce::Justification::centredLeft, false);
    content.removeFromTop (14.0f);

    const float gap = 14.0f;
    const float colW = (content.getWidth() - 2.0f * gap) / 3.0f;

    for (int c = 0; c < 3; ++c)
    {
        const auto& col = helpColumns[c];
        auto area = juce::Rectangle<float> (content.getX() + (float) c * (colW + gap), content.getY(), colW, content.getHeight());
        const auto colour = ModeSwitch::modeColour (c);
        const bool isCurrent = c == highlighted;

        g.setColour (isCurrent ? blend (Colours::panel, colour, 0.08f) : Colours::panel.withAlpha (0.6f));
        g.fillRoundedRectangle (area, 9.0f);
        g.setColour (isCurrent ? colour.withAlpha (0.6f) : Colours::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (area.reduced (0.5f), 9.0f, 1.0f);

        area = area.reduced (14.0f, 12.0f);
        auto head = area.removeFromTop (20.0f);
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ head.getX() + 3.5f, head.getCentreY() }));
        g.setFont (font (13.0f, 700).withExtraKerningFactor (0.08f));
        g.setColour (colour.brighter (0.3f));
        g.drawText (col.title, head.withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
        g.setFont (font (11.0f, 600));
        g.setColour (Colours::textSubtle);
        g.drawText (col.subtitle, area.removeFromTop (16.0f), juce::Justification::centredLeft, false);
        area.removeFromTop (8.0f);

        for (int s = 0; s < 4; ++s)
        {
            const bool isTip = juce::String (col.steps[s]).startsWith ("Tip:");
            auto row = area;
            const auto number = row.removeFromLeft (20.0f).withHeight (18.0f);

            if (! isTip)
            {
                g.setColour (colour.withAlpha (0.18f));
                g.fillEllipse (number.withSizeKeepingCentre (17.0f, 17.0f));
                g.setFont (font (10.5f, 700));
                g.setColour (colour.brighter (0.3f));
                g.drawText (juce::String (s + 1), number, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (Colours::textFaint);
                g.fillEllipse (number.withSizeKeepingCentre (5.0f, 5.0f));
            }

            row.removeFromLeft (6.0f);
            juce::TextLayout layout;
            layout.createLayout (richText (col.steps[s], 12.0f, isTip ? Colours::textSubtle : Colours::textDim, Colours::text, 2.0f),
                                 row.getWidth());
            layout.draw (g, row.withHeight (layout.getHeight()).translated (0.0f, 1.0f));
            area.removeFromTop (layout.getHeight() + 12.0f);
        }
    }
}

void HelpOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (isLeftClick (e) && onClose != nullptr)
        onClose();
}
} // namespace dk::ui
