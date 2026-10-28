#include "AllocationTracking.h"

#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>

// Processor-level integration tests: runs DucklingProcessor like a host would (play head, sidechain bus, MIDI,
// odd block sizes, state save / load) and checks the audio that comes out. Also checks that processBlock never
// allocates.

namespace
{
int failures = 0, passes = 0;

void check (bool ok, const std::string& what, const std::string& detail = {})
{
    (ok ? passes : failures)++;
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what;

    if (! ok && ! detail.empty())
        std::cout << "  (" << detail << ")";

    std::cout << "\n";
}

template <typename... Args>
std::string str (Args&&... args)
{
    std::ostringstream os;
    (os << ... << args);
    return os.str();
}

constexpr double kRate = 48000.0;

struct TestPlayHead final : public juce::AudioPlayHead
{
    bool playing = true, hasPosition = true;
    double bpm = 120.0;
    long long time = 0;   // samples since song start

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });

        if (hasPosition)
        {
            info.setTimeInSamples (time);
            info.setPpqPosition ((double) time / kRate * bpm / 60.0);
        }

        return info;
    }
};

struct Harness
{
    dk::DucklingProcessor proc;
    TestPlayHead head;
    juce::MidiBuffer midi;
    int blockSize = 512;
    long long inputIndex = 0;   // input signal position: always advances (the play head stops when the host stops)

    explicit Harness (int maxBlock = 512)
    {
        blockSize = maxBlock;
        proc.setPlayHead (&head);
        proc.setRateAndBufferSizeDetails (kRate, maxBlock);
        proc.prepareToPlay (kRate, maxBlock);
    }

    void set (const char* id, float plainValue)
    {
        auto* p = proc.getAPVTS().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    }

    // Runs `total` samples. Channels 0/1: main input from `mainIn`, channels 2/3: sidechain from `scIn` (when the
    // layout has a sidechain). Returns the left output.
    std::vector<float> run (int total, const std::function<float (long long)>& mainIn,
                            const std::function<float (long long)>& scIn = nullptr,
                            const std::vector<int>& noteOns = {}, int block = -1)
    {
        block = block > 0 ? block : blockSize;
        std::vector<float> out;
        out.reserve ((size_t) total);
        const int channels = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (channels, block);

        for (int start = 0; start < total; start += block)
        {
            const int n = juce::jmin (block, total - start);
            buffer.setSize (channels, n, false, false, true);
            buffer.clear();

            for (int i = 0; i < n; ++i)
            {
                const auto index = inputIndex + i;
                const float v = mainIn ? mainIn (index) : 0.0f;
                buffer.setSample (0, i, v);

                if (channels > 1)
                    buffer.setSample (1, i, v);

                if (scIn && proc.getTotalNumInputChannels() >= 4)
                {
                    const float s = scIn (index);
                    buffer.setSample (2, i, s);
                    buffer.setSample (3, i, s);
                }
            }

            midi.clear();

            for (int at : noteOns)
                if (at >= start && at < start + n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), at - start);

            dkdev::setAllocationTracking (true);
            proc.processBlock (buffer, midi);
            dkdev::setAllocationTracking (false);

            for (int i = 0; i < n; ++i)
                out.push_back (buffer.getSample (0, i));

            inputIndex += n;

            if (head.playing)
                head.time += n;
        }

        return out;
    }
};

float sine (long long i, float hz, float amp = 0.5f)
{
    return amp * std::sin (2.0f * juce::MathConstants<float>::pi * hz * (float) ((double) i / kRate));
}

// RMS of out / in over [from, to).
float ratio (const std::vector<float>& out, const std::function<float (long long)>& in, int from, int to)
{
    double a = 0.0, b = 0.0;

    for (int i = from; i < to; ++i)
    {
        a += (double) out[(size_t) i] * out[(size_t) i];
        const double x = in ((long long) i);
        b += x * x;
    }

    return b > 0.0 ? (float) std::sqrt (a / b) : 0.0f;
}

int beatSamples (double bpm) { return (int) std::lround (60.0 / bpm * kRate); }

//==============================================================================
void testTempo()
{
    std::cout << "\n-- Tempo mode\n";
    auto tone = [] (long long i) { return sine (i, 440.0f); };

    {
        Harness h;
        const int beat = beatSamples (120.0);
        auto out = h.run (beat * 4, tone);
        bool ok = true;
        std::string detail;

        for (int b = 0; b < 4; ++b)
        {
            const float early = ratio (out, tone, b * beat + 100, b * beat + 600);
            const float late = ratio (out, tone, b * beat + beat * 3 / 4, b * beat + beat - 50);
            ok &= early < 0.1f && late > 0.95f;
            detail += str ("beat ", b, ": ", early, " / ", late, "  ");
        }

        check (ok, "ducks right after every beat and is back before the next one", detail);

        const auto ds = h.proc.getDisplayState();
        check (ds.hostPlaying && ds.hostHasPosition && std::abs (ds.bpm - 120.0) < 1.0e-6, "display state follows the host");
        check (ds.triggerCount >= 4, "a trigger per beat", str (ds.triggerCount));
    }

    {
        Harness h;
        h.head.playing = false;
        auto out = h.run (24000, tone);
        check (ratio (out, tone, 0, 24000) > 0.9999f, "untouched while the host is stopped");
    }

    {
        Harness h;
        h.set (dk::ParamIDs::depth, 0.0f);
        auto out = h.run (24000, tone);
        float maxDiff = 0.0f;
        for (int i = 0; i < 24000; ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (out[(size_t) i] - tone (i)));
        check (maxDiff < 1.0e-6f, "depth 0 % is bit-transparent in Full band", str (maxDiff));
    }

    {
        Harness h;
        h.set (dk::ParamIDs::depth, 50.0f);
        auto out = h.run (beatSamples (120.0), tone);
        const float early = ratio (out, tone, 200, 600);
        check (std::abs (early - 0.5f) < 0.05f, "depth 50 % halves the level at the bottom of the duck", str (early));
    }

    {
        Harness h;
        h.set (dk::ParamIDs::rate, (float) dk::RateDivision::Eighth);
        h.head.bpm = 140.0;
        const int half = beatSamples (140.0) / 2;
        auto out = h.run (half * 4, tone, nullptr, {}, 333);
        bool ok = true;
        for (int k = 0; k < 4; ++k)
            ok &= ratio (out, tone, k * half + 100, k * half + 400) < 0.15f;
        check (ok, "1/8 rate at 140 BPM with odd 333-sample blocks ducks every 8th");
    }

    {
        Harness h;
        h.head.hasPosition = false;
        h.head.playing = false;
        auto out = h.run (beatSamples (120.0) * 2, tone);
        check (ratio (out, tone, beatSamples (120.0) + 100, beatSamples (120.0) + 600) < 0.1f,
               "no song position: runs on its own clock");
    }
}

void testBands()
{
    std::cout << "\n-- Band split\n";
    auto low = [] (long long i) { return sine (i, 50.0f); };
    auto high = [] (long long i) { return sine (i, 6000.0f); };
    const int beat = beatSamples (120.0);

    {
        Harness h;
        h.set (dk::ParamIDs::band, (float) dk::DuckBand::Lows);
        h.set (dk::ParamIDs::crossover, 150.0f);
        h.run (beat * 2, low);   // settle the filters
        auto outLow = h.run (beat, low);
        Harness h2;
        h2.set (dk::ParamIDs::band, (float) dk::DuckBand::Lows);
        h2.head.time = h2.inputIndex = beat * 2;
        auto outHigh = h2.run (beat, high);

        auto shifted = [beat] (const std::function<float (long long)>& f) {
            return [f, beat] (long long i) { return f (i + beat * 2); };
        };
        const float lowDuck = ratio (outLow, shifted (low), 400, 2400);
        const float highDuck = ratio (outHigh, shifted (high), 400, 2400);
        check (lowDuck < 0.15f, "Lows: 50 Hz is ducked", str (lowDuck));
        check (highDuck > 0.95f, "Lows: 6 kHz passes untouched", str (highDuck));
    }

    {
        Harness h;
        h.set (dk::ParamIDs::band, (float) dk::DuckBand::Highs);
        h.head.time = h.inputIndex = beat * 2;
        auto outHigh = h.run (beat, high);
        auto shifted = [beat, high] (long long i) { return high (i + beat * 2); };
        check (ratio (outHigh, shifted, 400, 2400) < 0.15f, "Highs: 6 kHz is ducked");
    }

    {
        // The split sums back flat when nothing is ducked (depth 0): only the phase changes.
        Harness h;
        h.set (dk::ParamIDs::band, (float) dk::DuckBand::Lows);
        h.set (dk::ParamIDs::depth, 0.0f);
        auto mid = [] (long long i) { return sine (i, 150.0f); };
        auto out = h.run (24000, mid);
        const float r = ratio (out, mid, 4000, 24000);
        check (std::abs (r - 1.0f) < 0.02f, "Lows at depth 0: level unchanged at the crossover", str (r));
    }
}

void testAudioTrigger()
{
    std::cout << "\n-- Audio (sidechain) mode\n";
    auto tone = [] (long long i) { return sine (i, 300.0f); };

    // Kicks every 300 ms (not on the host's beat, so Tempo mode could not explain the ducking).
    auto kicks = [] (long long i) {
        const long long period = 14400, offset = 5000;

        if (i < offset)
            return 0.0f;

        const long long t = (i - offset) % period;
        return t < 4000 ? 0.9f * std::exp (-(float) t / 900.0f) * std::sin (2.0f * 3.14159f * 55.0f * (float) t / 48000.0f) : 0.0f;
    };

    {
        Harness h;
        check (h.proc.getTotalNumInputChannels() == 4, "default layout: stereo main + stereo sidechain",
               str (h.proc.getTotalNumInputChannels()));
        h.set (dk::ParamIDs::trigger, (float) dk::TriggerMode::Audio);
        h.set (dk::ParamIDs::rate, (float) dk::RateDivision::Eighth);
        h.set (dk::ParamIDs::threshold, -20.0f);
        auto out = h.run (48000, tone, kicks);

        const float before = ratio (out, tone, 1000, 4900);
        const float k1 = ratio (out, tone, 5000 + 150, 5000 + 700);
        const float k2 = ratio (out, tone, 19400 + 150, 19400 + 700);
        const float between = ratio (out, tone, 5000 + 11000, 5000 + 14000);
        check (before > 0.999f, "no duck before the first kick", str (before));
        check (k1 < 0.1f && k2 < 0.1f, "ducks on every kick", str (k1, " / ", k2));
        check (between > 0.95f, "recovers between kicks", str (between));

        const auto ds = h.proc.getDisplayState();
        check (ds.sidechainChannels == 2 && ds.sidechainHeard, "display: sidechain connected and heard");
    }

    {
        Harness h;
        h.set (dk::ParamIDs::trigger, (float) dk::TriggerMode::Audio);
        auto out = h.run (48000, tone, nullptr);
        check (ratio (out, tone, 0, 48000) > 0.9999f, "silent sidechain: no ducking");
        check (! h.proc.getDisplayState().sidechainHeard, "display: sidechain not heard");
    }

    {
        // The host turned the sidechain bus off.
        Harness h;
        auto layout = h.proc.getBusesLayout();
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
        check (h.proc.setBusesLayout (layout), "sidechain bus can be disabled");
        h.proc.prepareToPlay (kRate, 512);
        h.set (dk::ParamIDs::trigger, (float) dk::TriggerMode::Audio);
        auto out = h.run (24000, tone);
        check (ratio (out, tone, 0, 24000) > 0.9999f, "no sidechain bus: plays through");
        check (h.proc.getDisplayState().sidechainChannels == 0, "display: no sidechain channels");
    }
}

void testMidiTrigger()
{
    std::cout << "\n-- MIDI mode\n";
    auto tone = [] (long long i) { return sine (i, 300.0f); };
    Harness h;
    h.set (dk::ParamIDs::trigger, (float) dk::TriggerMode::Midi);
    h.set (dk::ParamIDs::rate, (float) dk::RateDivision::Eighth);
    auto out = h.run (48000, tone, nullptr, { 3000, 30000 });

    check (ratio (out, tone, 100, 2900) > 0.999f, "no duck before the first note");
    check (ratio (out, tone, 3150, 3700) < 0.1f, "ducks on the first note");
    check (ratio (out, tone, 20000, 29000) > 0.999f, "recovered after the curve");
    check (ratio (out, tone, 30150, 30700) < 0.1f, "ducks on the second note");
    check (h.proc.getDisplayState().midiNoteCount == 2, "display counts the notes");
}

void testLayouts()
{
    std::cout << "\n-- Bus layouts\n";
    dk::DucklingProcessor proc;
    using CS = juce::AudioChannelSet;

    auto layout = [] (CS main, CS sc) {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (main);
        l.inputBuses.add (sc);
        l.outputBuses.add (main);
        return l;
    };

    check (proc.checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())), "stereo + stereo sidechain");
    check (proc.checkBusesLayoutSupported (layout (CS::stereo(), CS::mono())), "stereo + mono sidechain");
    check (proc.checkBusesLayoutSupported (layout (CS::mono(), CS::disabled())), "mono, no sidechain");
    check (! proc.checkBusesLayoutSupported (layout (CS::create5point1(), CS::stereo())), "5.1 rejected");

    juce::AudioProcessor::BusesLayout mismatch;
    mismatch.inputBuses.add (CS::mono());
    mismatch.inputBuses.add (CS::stereo());
    mismatch.outputBuses.add (CS::stereo());
    check (! proc.checkBusesLayoutSupported (mismatch), "mono in / stereo out rejected");

    // Mono main bus with a mono sidechain really processes.
    Harness h;
    check (h.proc.setBusesLayout (layout (CS::mono(), CS::mono())), "switch to mono + mono sidechain");
    h.proc.prepareToPlay (kRate, 512);
    auto tone = [] (long long i) { return sine (i, 440.0f); };
    auto out = h.run (beatSamples (120.0), tone);
    check (ratio (out, tone, 200, 600) < 0.1f, "mono: ducks");
}

void testState()
{
    std::cout << "\n-- State\n";
    juce::MemoryBlock state;
    {
        dk::DucklingProcessor a;
        auto set = [&a] (const char* id, float v) {
            auto* p = a.getAPVTS().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        set (dk::ParamIDs::trigger, 2.0f);
        set (dk::ParamIDs::rate, 4.0f);
        set (dk::ParamIDs::curve, 7.0f);
        set (dk::ParamIDs::depth, 63.0f);
        set (dk::ParamIDs::band, 1.0f);
        set (dk::ParamIDs::crossover, 220.0f);
        set (dk::ParamIDs::threshold, -31.5f);
        a.setEditorWidth (900);
        a.getStateInformation (state);
    }

    dk::DucklingProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    auto get = [&b] (const char* id) { return b.getAPVTS().getRawParameterValue (id)->load(); };
    check (get (dk::ParamIDs::trigger) == 2.0f && get (dk::ParamIDs::rate) == 4.0f && get (dk::ParamIDs::curve) == 7.0f
               && std::abs (get (dk::ParamIDs::depth) - 63.0f) < 0.05f && get (dk::ParamIDs::band) == 1.0f
               && std::abs (get (dk::ParamIDs::crossover) - 220.0f) < 1.0f
               && std::abs (get (dk::ParamIDs::threshold) + 31.5f) < 0.05f,
           "every parameter survives save / load");
    check (b.getEditorWidth() == 900, "editor size survives save / load");

    b.setStateInformation ("garbage", 7);
    check (true, "garbage state does not crash");
}

void testRobustness()
{
    std::cout << "\n-- Robustness\n";
    Harness h (8192);
    auto noisy = [] (long long i) { return (i % 1000 == 7) ? std::numeric_limits<float>::quiet_NaN() : sine (i, 220.0f); };
    auto out = h.run (8192 * 3, [] (long long i) { return sine (i, 220.0f); }, nullptr, {}, 8192);
    bool finite = true;
    for (float v : out)
        finite &= std::isfinite (v);
    check (finite, "8192-sample blocks (larger than prepared chunk) stay finite");

    Harness h2;
    h2.set (dk::ParamIDs::trigger, (float) dk::TriggerMode::Audio);
    h2.run (48000, [] (long long i) { return sine (i, 220.0f); }, noisy);
    auto after = h2.run (4800, [] (long long i) { return sine (i, 220.0f); });
    bool finite2 = true;
    for (float v : after)
        finite2 &= std::isfinite (v);
    check (finite2, "NaNs on the sidechain never poison the output");

    Harness h3;
    auto single = h3.run (2000, [] (long long i) { return sine (i, 220.0f); }, nullptr, {}, 1);
    check (single.size() == 2000, "1-sample blocks work");
}

void testAllocations()
{
    std::cout << "\n-- Realtime\n";
    const auto hooked = dkdev::installHeapHooks();
    check (hooked.find ("malloc") != std::string::npos, "heap hooks installed (" + hooked + ")");

    // Sanity: the counter sees an allocation.
    const auto before = dkdev::getAllocationCounts();
    dkdev::setAllocationTracking (true);
    {
        juce::AudioBuffer<float> b (2, 64);
        b.clear();
    }
    dkdev::setAllocationTracking (false);
    check (dkdev::getAllocationCounts().allocations > before.allocations, "allocation counter works");

    // All the runs above went through Harness::run with tracking on; repeat a mixed run and count only processBlock.
    Harness h;
    h.set (dk::ParamIDs::trigger, (float) dk::TriggerMode::Audio);
    h.set (dk::ParamIDs::band, (float) dk::DuckBand::Lows);
    juce::AudioBuffer<float> buffer (4, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 10);
    midi.clear();
    const auto start = dkdev::getAllocationCounts();

    for (int i = 0; i < 50; ++i)
    {
        for (int s = 0; s < 512; ++s)
            buffer.setSample (2, s, (s + i * 512) % 9600 < 200 ? 0.8f : 0.0f);

        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), i % 512);
        dkdev::setAllocationTracking (true);
        h.proc.processBlock (buffer, midi);
        dkdev::setAllocationTracking (false);
        midi.clear();
    }

    const auto end = dkdev::getAllocationCounts();
    check (end.allocations == start.allocations && end.frees == start.frees, "processBlock never allocates",
           str (end.allocations - start.allocations, " allocations"));
}

void testEditor()
{
    std::cout << "\n-- Editor\n";
    Harness h;
    std::unique_ptr<juce::AudioProcessorEditor> editor (h.proc.createEditorAndMakeActive());
    check (editor != nullptr && editor->getWidth() == 720 && editor->getHeight() == 480, "opens at 720 x 480",
           editor != nullptr ? str (editor->getWidth(), "x", editor->getHeight()) : "null");

    if (editor != nullptr)
    {
        editor->setSize (1080, 720);
        check (h.proc.getEditorWidth() == 1080, "remembers its size");
        h.set (dk::ParamIDs::trigger, 1.0f);
        h.set (dk::ParamIDs::curve, 11.0f);
        h.run (4800, [] (long long i) { return sine (i, 100.0f); });
        if (auto* e = dynamic_cast<dk::ui::DucklingEditor*> (editor.get()))
            e->refreshNow();
        h.proc.editorBeingDeleted (editor.get());
        editor.reset();
        check (true, "closes cleanly");
    }
}
} // namespace

int runSelfTest()
{
    std::cout << "Duckling self-test\n";
    dkdev::installHeapHooks();

    testTempo();
    testBands();
    testAudioTrigger();
    testMidiTrigger();
    testLayouts();
    testState();
    testRobustness();
    testAllocations();
    testEditor();

    std::cout << "\n" << passes << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
