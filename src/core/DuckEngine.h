#pragma once

// The ducking engine: turns a trigger source (the host's tempo, a sidechain signal or MIDI notes) into a per-sample
// gain. Pure C++17, no JUCE, no allocation after prepare(), safe to call from the audio thread.

#include "core/Curves.h"

#include <cstdint>

namespace dk
{
enum class TriggerMode
{
    Tempo = 0,   // ducks on every Rate step of the host's song position (no routing needed). Stored in projects:
    Audio,       // a sidechain signal crossing the threshold starts one curve          only ever append.
    Midi,        // every note-on starts one curve
    NumModes
};

constexpr int numTriggerModes = (int) TriggerMode::NumModes;
const char* triggerModeName (TriggerMode);   // "Tempo", "Audio", "MIDI"

enum class RateDivision
{
    Sixteenth = 0,   // stored in projects: only ever append
    Eighth,
    Quarter,
    Half,
    Bar,
    TwoBars,
    NumRates
};

constexpr int numRateDivisions = (int) RateDivision::NumRates;
const char* rateName (RateDivision);   // "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars"

// Length of one duck cycle in quarter-note beats. Bars follow the time signature (beatsPerBar quarter notes, 4 in
// 4/4, 3 in 3/4, 3 in 6/8).
double rateInBeats (RateDivision, double beatsPerBar = 4.0) noexcept;

struct DuckSettings
{
    TriggerMode mode = TriggerMode::Tempo;
    CurveShape curve = CurveShape::Classic;
    double rateBeats = 1.0;        // one duck cycle, in quarter-note beats (> 0)
    float depth = 1.0f;            // 0..1: how far a full duck pulls the volume down (1 = to silence)
    float thresholdDb = -24.0f;    // Audio mode: sidechain level that triggers a duck
};

struct TransportInfo
{
    bool hasPosition = false;   // false: no host song position (stand-alone hosts) -> Tempo mode runs a free clock
    bool isPlaying = false;
    double ppq = 0.0;           // song position at the first sample, in quarter notes
    double bpm = 120.0;         // <= 0 or non-finite: 120
};

class DuckEngine
{
public:
    // Gain smoothing (prevents clicks when a curve starts or the depth jumps).
    static constexpr float kAttackMs = 0.8f;    // gain falling
    static constexpr float kReleaseMs = 2.0f;   // gain rising
    // Audio trigger detector.
    static constexpr float kDetectorReleaseMs = 35.0f;   // peak envelope decay
    static constexpr float kRearmHysteresisDb = 6.0f;    // the envelope must fall this far below the threshold to re-arm
    static constexpr float kMinRetriggerMs = 40.0f;      // never two audio triggers closer than this
    static constexpr float kFloorDb = -100.0f;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // Writes the linear gain (0..1, 1 = untouched) for numSamples samples into gains.
    //   detector:      Audio mode input, one value per sample, typically max |channel| of the sidechain; may be null
    //                  (then nothing triggers and the meter falls).
    //   midiTriggers:  sample offsets (ascending, 0..numSamples-1) of this block's note-ons for MIDI mode.
    // The transport describes the first sample; the position advances at the given tempo through the block.
    void process (const DuckSettings&, const TransportInfo&, const float* detector, const int* midiTriggers,
                  int numMidiTriggers, float* gains, int numSamples) noexcept;

    // State after the last process() call (for the editor).
    float getPhase() const noexcept           { return displayPhase; }   // 0..1 inside a duck cycle, -1 when idle
    float getGain() const noexcept            { return gain; }
    float getDetectorLevelDb() const noexcept;                           // Audio mode envelope, kFloorDb when silent
    uint32_t getTriggerCount() const noexcept { return triggerCount; }   // +1 for every cycle start

private:
    void startCycle() noexcept;

    double sampleRate = 44100.0;
    float attackCoef = 1.0f, releaseCoef = 1.0f, detectorCoef = 0.0f;
    int minRetriggerSamples = 1;

    float gain = 1.0f;
    double cyclePhase = 2.0;     // triggered modes: >= 1 means idle
    double freeRunPpq = 0.0;     // Tempo mode clock without a host position
    double lastTempoPhase = -1.0;
    float envelope = 0.0f;
    bool armed = true;
    int samplesSinceTrigger = 1 << 30;
    TriggerMode lastMode = TriggerMode::Tempo;
    float displayPhase = -1.0f;
    uint32_t triggerCount = 0;
};
} // namespace dk
