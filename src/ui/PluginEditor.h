#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/Panels.h"

#include <optional>

namespace dk::ui
{
// The plugin window. Everything is laid out on a fixed 720 x 480 design canvas that is scaled as a whole when the
// window is resized (the aspect ratio is fixed), so it looks the same at every size.
class DucklingEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DucklingEditor (DucklingProcessor&);
    ~DucklingEditor() override;

    static constexpr int kDesignWidth = 720;
    static constexpr int kDesignHeight = 480;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Dev tool / tests: show the help overlay, replace the live processor state with a fixed one, and push the
    // current state into the panels right away (instead of waiting for the next timer tick).
    void setHelpVisible (bool);
    void setDisplayOverride (std::optional<DucklingProcessor::DisplayState>, float flash = 0.0f);
    void refreshNow();

private:
    void timerCallback() override;

    struct Content;
    DucklingProcessor& processor;
    LookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;
    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DucklingEditor)
};
} // namespace dk::ui
