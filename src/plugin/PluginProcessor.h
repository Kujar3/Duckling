#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "core/DuckEngine.h"

#include <array>
#include <atomic>

namespace dk
{
namespace ParamIDs
{
    inline constexpr const char* trigger   = "trigger";     // choice: Tempo, Audio, MIDI
    inline constexpr const char* rate      = "rate";        // choice: 1/16 .. 2 bars
    inline constexpr const char* curve     = "curve";       // choice: curve shapes
    inline constexpr const char* depth     = "depth";       // 0..100 %
    inline constexpr const char* band      = "band";        // choice: Full, Lows, Highs
    inline constexpr const char* crossover = "crossover";   // 40..1000 Hz
    inline constexpr const char* threshold = "threshold";   // -60..0 dB
}

// What the Band choice ducks.
enum class DuckBand { Full = 0, Lows, Highs, NumBands };
const char* duckBandName (DuckBand);   // "Full", "Lows", "Highs"

//==============================================================================
// Duckling: a volume-shaping / sidechain ducking effect.
// Buses: main stereo (or mono) in/out, plus an optional "Sidechain" input (stereo or mono) for Audio trigger mode.
// MIDI input: note-ons trigger the curve in MIDI mode. No latency, no tail.
class DucklingProcessor final : public juce::AudioProcessor
{
public:
    DucklingProcessor();
    ~DucklingProcessor() override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    // Live state for the editor (written by the audio thread after every block, read on the message thread).
    struct DisplayState
    {
        float phase = -1.0f;            // 0..1 inside a duck cycle, -1 idle
        float gain = 1.0f;              // current gain (linear)
        float sidechainDb = DuckEngine::kFloorDb;
        uint32_t triggerCount = 0;      // +1 for every duck cycle that starts
        uint32_t midiNoteCount = 0;     // note-ons received since load
        bool hostHasPosition = false;
        bool hostPlaying = false;
        double bpm = 120.0;
        double beatsPerBar = 4.0;
        int sidechainChannels = 0;      // 0 = the host gave no sidechain input
        bool sidechainHeard = false;    // any sidechain signal above -70 dBFS in the last 2 seconds
        bool processing = false;        // processBlock ran in the last half second
    };

    DisplayState getDisplayState() const noexcept;

    // Editor size (logical pixels), saved with the project.
    int getEditorWidth() const noexcept { return editorWidth.load(); }
    void setEditorWidth (int width) noexcept { editorWidth.store (width); }

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Duckling"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr int kDefaultEditorWidth = 720;

private:
    static constexpr int kChunk = 256;        // internal processing block (fixed-size scratch, no allocation)
    static constexpr int kMaxMidiPerChunk = 128;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* triggerParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* curveParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* bandParam = nullptr;
    std::atomic<float>* crossoverParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;

    DuckEngine engine;
    juce::dsp::LinkwitzRileyFilter<float> crossover;
    float currentCrossoverHz = -1.0f;
    double currentSampleRate = 44100.0;

    std::array<float, kChunk> detector {};
    std::array<float, kChunk> gains {};
    std::array<int, kMaxMidiPerChunk> midiOffsets {};

    int samplesSinceSidechainSignal = 1 << 30;

    // Display state
    std::atomic<float> displayPhase { -1.0f }, displayGain { 1.0f }, displaySidechainDb { DuckEngine::kFloorDb };
    std::atomic<uint32_t> displayTriggers { 0 }, midiNotes { 0 };
    std::atomic<bool> displayHasPosition { false }, displayPlaying { false }, displaySidechainHeard { false };
    std::atomic<double> displayBpm { 120.0 }, displayBeatsPerBar { 4.0 };
    std::atomic<int> displaySidechainChannels { 0 };
    std::atomic<juce::uint32> lastProcessMs { 0 };

    std::atomic<int> editorWidth { kDefaultEditorWidth };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DucklingProcessor)
};
} // namespace dk
