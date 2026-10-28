#pragma once

#include "ui/Theme.h"

#include <functional>

// Small building blocks shared by the editor's panels: number formatting, the logo, icon buttons, segmented choosers
// and the big three-way trigger switch.
namespace dk::ui
{
//==============================================================================
// Text formatting (ASCII only so every font renders it).
namespace fmt
{
    juce::String number (float value, int decimals);   // "-11.8" (no "-0.0")
    juce::String gainDb (float linearGain);           // "0.0 dB", "-6.0 dB", "-inf dB"
}

// Rich text: "**bold**" runs in `strong`, the rest in `normal`. Word-wrapped, top-left.
juce::AttributedString richText (const juce::String& markup, float size, juce::Colour normal, juce::Colour strong,
                                 float lineSpacing = 2.0f);

// Phase 0..1 of a looping animation with the given period. The dev tool freezes the clock so snapshots are
// deterministic.
float animationPhase (juce::uint32 periodMs);
void freezeAnimationsForSnapshots (bool shouldFreeze);
bool animationsAreFrozen();

// Mixes two colours (alpha kept from `a`).
juce::Colour blend (juce::Colour a, juce::Colour b, float amountOfB);

// Duckling's logo: the embedded logo.png (resource "logo_png") when present, else a vector-drawn mint / lavender
// mark. `area` should be square.
void drawLogo (juce::Graphics&, juce::Rectangle<float> area);

//==============================================================================
// Small icon-only button (header tools, close buttons). Only left clicks act (right / middle clicks are ignored).
class IconButton final : public juce::Button
{
public:
    enum class Icon { help, close };

    IconButton (const juce::String& name, Icon iconType);

    void mouseDown (const juce::MouseEvent& e) override { if (isLeftClick (e)) juce::Button::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (isLeftClick (e)) juce::Button::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (isLeftClick (e)) juce::Button::mouseUp (e); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    static void paintIcon (juce::Graphics&, Icon, juce::Rectangle<float> box);

    bool roundBackground = false;   // circular hover fill (close buttons)

private:
    Icon icon;
};

//==============================================================================
// A row (or grid) of mutually exclusive pill segments in one dark track, like Chordbloom's Snap / Grid choosers.
// Left clicks pick; the selected segment is raised with an accent rim. `columns` > 0 wraps into a grid.
class Segmented final : public juce::Component, public juce::TooltipClient
{
public:
    explicit Segmented (juce::StringArray items, int columns = 0);

    void setSelected (int index);   // no callback
    int getSelected() const noexcept { return selected; }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setItemTooltips (juce::StringArray tips) { tooltips = std::move (tips); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Rectangle<float> segmentBounds (int index) const;
    int indexAt (juce::Point<float>) const;

    juce::StringArray items, tooltips;
    int columns = 0, selected = 0, hovered = -1;
    juce::Colour accent = Colours::accent;
};

//==============================================================================
// The big "TEMPO | AUDIO | MIDI" trigger switch in the header. The highlight slides to the chosen mode; each segment
// has a sub-label ("follows FL", "sidechain", "notes") and its own colour.
class ModeSwitch final : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    ModeSwitch();

    void setSelected (int index);   // no callback, animates
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onChange;

    static juce::Colour modeColour (int index);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    juce::String getTooltip() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void timerCallback() override;
    int indexAt (float x) const;

    int selected = 0;
    float position = 0.0f;   // displayed highlight position (0..2)
};

} // namespace dk::ui
