#include "core/DuckEngine.h"

#include <algorithm>
#include <cmath>

namespace dk
{
namespace
{
float onePoleCoef (double sampleRate, float milliseconds) noexcept
{
    const double samples = std::max (1.0, sampleRate * (double) milliseconds * 0.001);
    return (float) (1.0 - std::exp (-1.0 / samples));
}

double wrap01 (double x) noexcept
{
    const double f = x - std::floor (x);
    return f >= 1.0 ? 0.0 : f;
}

double safeBpm (double bpm) noexcept
{
    return (std::isfinite (bpm) && bpm >= 1.0) ? std::min (bpm, 999.0) : 120.0;
}

float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
} // namespace

const char* triggerModeName (TriggerMode mode)
{
    switch (mode)
    {
        case TriggerMode::Tempo: return "Tempo";
        case TriggerMode::Audio: return "Audio";
        case TriggerMode::Midi:  return "MIDI";
        case TriggerMode::NumModes: break;
    }

    return "Tempo";
}

const char* rateName (RateDivision rate)
{
    switch (rate)
    {
        case RateDivision::Sixteenth: return "1/16";
        case RateDivision::Eighth:    return "1/8";
        case RateDivision::Quarter:   return "1/4";
        case RateDivision::Half:      return "1/2";
        case RateDivision::Bar:       return "1 bar";
        case RateDivision::TwoBars:   return "2 bars";
        case RateDivision::NumRates:  break;
    }

    return "1/4";
}

double rateInBeats (RateDivision rate, double beatsPerBar) noexcept
{
    const double bar = (std::isfinite (beatsPerBar) && beatsPerBar > 0.0) ? beatsPerBar : 4.0;

    switch (rate)
    {
        case RateDivision::Sixteenth: return 0.25;
        case RateDivision::Eighth:    return 0.5;
        case RateDivision::Quarter:   return 1.0;
        case RateDivision::Half:      return 2.0;
        case RateDivision::Bar:       return bar;
        case RateDivision::TwoBars:   return 2.0 * bar;
        case RateDivision::NumRates:  break;
    }

    return 1.0;
}

//==============================================================================
void DuckEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = (std::isfinite (newSampleRate) && newSampleRate > 0.0) ? newSampleRate : 44100.0;
    attackCoef = onePoleCoef (sampleRate, kAttackMs);
    releaseCoef = onePoleCoef (sampleRate, kReleaseMs);
    detectorCoef = (float) std::exp (-1.0 / std::max (1.0, sampleRate * kDetectorReleaseMs * 0.001));
    minRetriggerSamples = std::max (1, (int) std::lround (sampleRate * kMinRetriggerMs * 0.001));
    reset();
}

void DuckEngine::reset() noexcept
{
    gain = 1.0f;
    cyclePhase = 2.0;
    freeRunPpq = 0.0;
    lastTempoPhase = -1.0;
    envelope = 0.0f;
    armed = true;
    samplesSinceTrigger = 1 << 30;
    displayPhase = -1.0f;
}

float DuckEngine::getDetectorLevelDb() const noexcept
{
    return envelope > 1.0e-5f ? std::max (kFloorDb, 20.0f * std::log10 (envelope)) : kFloorDb;
}

void DuckEngine::startCycle() noexcept
{
    cyclePhase = 0.0;
    samplesSinceTrigger = 0;
    ++triggerCount;
}

void DuckEngine::process (const DuckSettings& settings, const TransportInfo& transport, const float* detector,
                          const int* midiTriggers, int numMidiTriggers, float* gains, int numSamples) noexcept
{
    if (gains == nullptr || numSamples <= 0)
        return;

    if (settings.mode != lastMode)
    {
        // A running curve from the previous mode would otherwise finish on its own; start the new mode clean.
        cyclePhase = 2.0;
        lastTempoPhase = -1.0;
        armed = true;
        lastMode = settings.mode;
    }

    const auto& spec = curveSpec (settings.curve);
    const double bpm = safeBpm (transport.bpm);
    const double rateBeats = (std::isfinite (settings.rateBeats) && settings.rateBeats > 1.0e-3) ? settings.rateBeats : 1.0;
    const double beatsPerSample = bpm / (60.0 * sampleRate);
    const double cycleIncrement = beatsPerSample / rateBeats;
    const float depth = std::clamp (std::isfinite (settings.depth) ? settings.depth : 1.0f, 0.0f, 1.0f);
    const float threshold = dbToGain (std::clamp (settings.thresholdDb, -80.0f, 0.0f));
    const float rearmLevel = threshold * dbToGain (-kRearmHysteresisDb);

    const bool tempoRunning = settings.mode == TriggerMode::Tempo && (! transport.hasPosition || transport.isPlaying);
    const double startPpq = transport.hasPosition ? (std::isfinite (transport.ppq) ? transport.ppq : 0.0) : freeRunPpq;

    int nextMidi = 0;
    float lastPhase = -1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float duck = 0.0f;

        // The detector always runs so the meter shows the sidechain in every mode.
        const float in = detector != nullptr ? std::abs (detector[i]) : 0.0f;
        envelope = (std::isfinite (in) && in > envelope) ? in : envelope * detectorCoef;

        if (samplesSinceTrigger < (1 << 30))
            ++samplesSinceTrigger;

        switch (settings.mode)
        {
            case TriggerMode::Tempo:
            {
                if (tempoRunning)
                {
                    const double phase = wrap01 ((startPpq + (double) i * beatsPerSample) / rateBeats);

                    if (lastTempoPhase < 0.0 || phase < lastTempoPhase)
                        ++triggerCount;

                    lastTempoPhase = phase;
                    duck = duckAmount (spec, (float) phase);
                    lastPhase = (float) phase;
                }
                else
                {
                    lastTempoPhase = -1.0;
                }
                break;
            }

            case TriggerMode::Audio:
            {
                if (armed && envelope >= threshold && samplesSinceTrigger >= minRetriggerSamples)
                {
                    startCycle();
                    armed = false;
                }
                else if (! armed && envelope < rearmLevel)
                {
                    armed = true;
                }
                break;
            }

            case TriggerMode::Midi:
            {
                bool triggered = false;

                while (midiTriggers != nullptr && nextMidi < numMidiTriggers && midiTriggers[nextMidi] <= i)
                {
                    ++nextMidi;
                    triggered = true;
                }

                if (triggered)
                    startCycle();
                break;
            }

            case TriggerMode::NumModes:
                break;
        }

        if (settings.mode != TriggerMode::Tempo && cyclePhase < 1.0)
        {
            duck = duckAmount (spec, (float) cyclePhase);
            lastPhase = (float) cyclePhase;
            cyclePhase += cycleIncrement;
        }
        else if (settings.mode != TriggerMode::Tempo)
        {
            lastPhase = -1.0f;
        }

        const float target = 1.0f - depth * duck;
        gain += (target - gain) * (target < gain ? attackCoef : releaseCoef);
        gains[i] = gain;
    }

    if (! transport.hasPosition)
        freeRunPpq = std::fmod (freeRunPpq + (double) numSamples * beatsPerSample, rateBeats * 4096.0);   // whole cycles

    displayPhase = lastPhase;
}
} // namespace dk
