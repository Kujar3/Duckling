#include "TestFramework.h"

#include "core/DuckEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace dk;

namespace
{
constexpr double kRate = 48000.0;

struct Run
{
    std::vector<float> gains;
    DuckEngine engine;

    explicit Run (double sampleRate = kRate) { engine.prepare (sampleRate); }

    // Processes `total` samples in blocks of `block`, advancing the transport like a host.
    void tempo (const DuckSettings& s, TransportInfo t, int total, int block = 512)
    {
        gains.assign ((size_t) total, 1.0f);

        for (int start = 0; start < total; start += block)
        {
            const int n = std::min (block, total - start);
            engine.process (s, t, nullptr, nullptr, 0, gains.data() + start, n);

            if (t.hasPosition && t.isPlaying)
                t.ppq += (double) n * t.bpm / (60.0 * kRate);
        }
    }
};

int samplesPerBeat (double bpm) { return (int) std::lround (60.0 / bpm * kRate); }

float minIn (const std::vector<float>& v, int from, int to)
{
    return *std::min_element (v.begin() + from, v.begin() + to);
}
} // namespace

TEST_CASE ("tempo mode ducks on every beat while playing")
{
    DuckSettings s;
    s.curve = CurveShape::Classic;
    s.rateBeats = 1.0;
    TransportInfo t { true, true, 0.0, 120.0 };

    Run r;
    const int beat = samplesPerBeat (120.0);
    r.tempo (s, t, beat * 4);

    for (int b = 0; b < 4; ++b)
    {
        // Deep in the duck a few ms after the beat, back to (nearly) full volume before the next one.
        CHECK (r.gains[(size_t) (b * beat + 200)] < 0.05f);
        CHECK (r.gains[(size_t) (b * beat + beat - 200)] > 0.98f);
    }

    CHECK_EQ (r.engine.getTriggerCount(), 4u);
}

TEST_CASE ("tempo mode follows the song position, not the block start")
{
    DuckSettings s;
    s.rateBeats = 1.0;
    // Start half a beat in: the first duck is half a beat later.
    TransportInfo t { true, true, 0.5, 120.0 };
    Run r;
    const int beat = samplesPerBeat (120.0);
    r.tempo (s, t, beat * 2, 333);

    CHECK (minIn (r.gains, 0, beat / 2 - 100) > 0.9f);
    CHECK (r.gains[(size_t) (beat / 2 + 200)] < 0.05f);
}

TEST_CASE ("tempo mode is silent-free and untouched while the host is stopped")
{
    DuckSettings s;
    TransportInfo t { true, false, 0.0, 120.0 };
    Run r;
    r.tempo (s, t, 48000);
    CHECK (minIn (r.gains, 0, 48000) > 0.999f);
    CHECK (r.engine.getPhase() < 0.0f);
}

TEST_CASE ("tempo mode free-runs without a host position")
{
    DuckSettings s;
    s.rateBeats = 1.0;
    TransportInfo t { false, false, 0.0, 120.0 };
    Run r;
    const int beat = samplesPerBeat (120.0);
    r.tempo (s, t, beat * 3, 256);
    CHECK (r.gains[(size_t) (beat + 300)] < 0.05f);
    CHECK (r.gains[(size_t) (2 * beat + 300)] < 0.05f);
}

TEST_CASE ("rates and bars")
{
    CHECK_EQ (rateInBeats (RateDivision::Sixteenth), 0.25);
    CHECK_EQ (rateInBeats (RateDivision::Quarter), 1.0);
    CHECK_EQ (rateInBeats (RateDivision::Bar), 4.0);
    CHECK_EQ (rateInBeats (RateDivision::TwoBars, 3.0), 6.0);
    CHECK_EQ (rateInBeats (RateDivision::Bar, 0.0), 4.0);

    DuckSettings s;
    s.rateBeats = 0.5;   // 1/8: two ducks per beat
    TransportInfo t { true, true, 0.0, 120.0 };
    Run r;
    r.tempo (s, t, samplesPerBeat (120.0) * 2);
    CHECK_EQ (r.engine.getTriggerCount(), 4u);
}

TEST_CASE ("depth scales the duck")
{
    DuckSettings s;
    s.depth = 0.5f;
    TransportInfo t { true, true, 0.0, 120.0 };
    Run r;
    r.tempo (s, t, samplesPerBeat (120.0));
    const float lowest = minIn (r.gains, 0, samplesPerBeat (120.0));
    CHECK (std::abs (lowest - 0.5f) < 0.01f);

    s.depth = 0.0f;
    Run r2;
    r2.tempo (s, t, samplesPerBeat (120.0));
    CHECK (minIn (r2.gains, 0, samplesPerBeat (120.0)) > 0.9999f);
}

TEST_CASE ("gain changes are smoothed (no instant jumps)")
{
    DuckSettings s;
    s.curve = CurveShape::Gate;   // the hardest edges
    s.rateBeats = 0.25;
    TransportInfo t { true, true, 0.0, 174.0 };
    Run r;
    r.tempo (s, t, 48000, 128);

    float maxStep = 0.0f;
    for (size_t i = 1; i < r.gains.size(); ++i)
        maxStep = std::max (maxStep, std::abs (r.gains[i] - r.gains[i - 1]));

    // ~0.8 ms attack at 48 kHz: at most a few percent per sample.
    CHECK (maxStep < 0.03f);
}

TEST_CASE ("audio mode triggers once per hit and re-arms after it decays")
{
    DuckSettings s;
    s.mode = TriggerMode::Audio;
    s.thresholdDb = -20.0f;
    s.rateBeats = 0.5;
    TransportInfo t { true, true, 0.0, 120.0 };

    const int total = 48000;
    std::vector<float> sc ((size_t) total, 0.0f);

    // Two "kicks" (decaying 60 Hz bursts) 250 ms apart, plus a quiet hat below the threshold.
    auto kick = [&] (int at) {
        for (int i = 0; i < 4800 && at + i < total; ++i)
            sc[(size_t) (at + i)] = 0.9f * std::exp (-(float) i / 1200.0f) * std::sin (2.0f * 3.14159265f * 60.0f * (float) i / 48000.0f);
    };
    kick (1000);
    kick (13000);
    for (int i = 30000; i < 30200; ++i)
        sc[(size_t) i] = 0.02f;   // -34 dB

    DuckEngine e;
    e.prepare (kRate);
    std::vector<float> gains ((size_t) total);

    for (int start = 0; start < total; start += 480)
        e.process (s, t, sc.data() + start, nullptr, 0, gains.data() + start, 480);

    CHECK_EQ (e.getTriggerCount(), 2u);
    CHECK (gains[1000 + 100] < 0.2f);
    CHECK (gains[13000 + 100] < 0.2f);
    CHECK (gains[900] > 0.999f);
    CHECK (minIn (gains, 28000, 31000) > 0.99f);   // the hat does not trigger
}

namespace
{
// A kick: a falling 120 -> 50 Hz sweep with a decaying tail (tau = tailMs), peak `amp`.
void addKick (std::vector<float>& sc, int at, float amp, float tailMs)
{
    double phase = 0.0;

    for (int i = 0; at + i < (int) sc.size() && i < 48000; ++i)
    {
        const float t = (float) i / 48000.0f;
        const float hz = 50.0f + 70.0f * std::exp (-t / 0.03f);
        phase += 2.0 * 3.14159265358979 * hz / 48000.0;
        sc[(size_t) (at + i)] += amp * std::exp (-t * 1000.0f / tailMs) * (float) std::sin (phase);
    }
}

int countDucks (const std::vector<float>& gains, const std::vector<int>& kicks, int& missedAt)
{
    int ducked = 0;
    missedAt = -1;

    for (int k : kicks)
    {
        if (gains[(size_t) (k + 240)] < 0.2f)   // 5 ms after the kick
            ++ducked;
        else if (missedAt < 0)
            missedAt = k;
    }

    return ducked;
}
} // namespace

TEST_CASE ("audio mode: uneven kick patterns (1/4 1/4 1/4 1/8 1/8) never miss a kick")
{
    // At 128 BPM, kicks on beats 0 1 2 3 3.5 | 4 5 6 7 7.5 | 8, each with a long-ish tail, default threshold.
    const double beat = 60.0 / 128.0 * 48000.0;
    const double beats[] = { 0, 1, 2, 3, 3.5, 4, 5, 6, 7, 7.5, 8 };

    for (const float tailMs : { 40.0f, 90.0f, 150.0f })
    {
        for (const float thresholdDb : { -24.0f, -12.0f })
        {
            std::vector<int> kicks;
            std::vector<float> sc ((size_t) (beat * 9.0), 0.0f);

            for (double b : beats)
            {
                kicks.push_back (2000 + (int) std::lround (b * beat));
                addKick (sc, kicks.back(), 0.9f, tailMs);
            }

            DuckSettings s;
            s.mode = TriggerMode::Audio;
            s.thresholdDb = thresholdDb;
            s.rateBeats = 0.5;
            s.curve = CurveShape::Punchy;
            TransportInfo t { true, true, 0.0, 128.0 };
            DuckEngine e;
            e.prepare (kRate);
            std::vector<float> gains (sc.size());

            for (size_t start = 0; start < sc.size(); start += 256)
            {
                const int n = (int) std::min<size_t> (256, sc.size() - start);
                e.process (s, t, sc.data() + start, nullptr, 0, gains.data() + start, n);
            }

            int missedAt = -1;
            const int ducked = countDucks (gains, kicks, missedAt);

            if (ducked != (int) kicks.size())
                std::cout << "    tail " << tailMs << " ms, threshold " << thresholdDb << " dB: " << ducked << "/"
                          << kicks.size() << " ducked, first miss at beat " << ((double) missedAt - 2000.0) / beat << "\n";

            CHECK_EQ (ducked, (int) kicks.size());
            CHECK_EQ (e.getTriggerCount(), (uint32_t) kicks.size());
        }
    }
}

TEST_CASE ("audio mode: kick rolls (1/16 and 1/8T) trigger once per kick, never twice")
{
    for (const double spacingBeats : { 0.25, 1.0 / 3.0 })
    {
        for (const double bpm : { 128.0, 150.0 })
        {
            const double beat = 60.0 / bpm * 48000.0;
            std::vector<int> kicks;
            std::vector<float> sc ((size_t) (beat * 5.0), 0.0f);

            for (double b = 0.0; b < 4.0 - 1.0e-9; b += spacingBeats)
            {
                kicks.push_back (2000 + (int) std::lround (b * beat));
                addKick (sc, kicks.back(), 0.9f, 60.0f);
            }

            DuckSettings s;
            s.mode = TriggerMode::Audio;
            s.thresholdDb = -24.0f;
            s.rateBeats = 0.25;
            s.curve = CurveShape::Classic;   // still deep 5 ms after the kick at 1/16
            TransportInfo t { true, true, 0.0, bpm };
            DuckEngine e;
            e.prepare (kRate);
            std::vector<float> gains (sc.size());
            e.process (s, t, sc.data(), nullptr, 0, gains.data(), (int) sc.size());

            int missedAt = -1;
            CHECK_EQ (countDucks (gains, kicks, missedAt), (int) kicks.size());
            CHECK_EQ (e.getTriggerCount(), (uint32_t) kicks.size());
        }
    }
}

TEST_CASE ("audio mode: a sustained signal above the threshold triggers only once")
{
    DuckSettings s;
    s.mode = TriggerMode::Audio;
    s.thresholdDb = -12.0f;
    TransportInfo t;
    DuckEngine e;
    e.prepare (kRate);
    std::vector<float> sc (48000), gains (48000);

    for (size_t i = 0; i < sc.size(); ++i)
        sc[i] = 0.8f * std::sin (2.0f * 3.14159265f * 50.0f * (float) i / 48000.0f);

    e.process (s, t, sc.data(), nullptr, 0, gains.data(), 48000);
    CHECK_EQ (e.getTriggerCount(), 1u);
    CHECK (e.getDetectorLevelDb() > -6.0f);
}

TEST_CASE ("MIDI mode starts a cycle at each note-on offset")
{
    DuckSettings s;
    s.mode = TriggerMode::Midi;
    s.rateBeats = 1.0;
    TransportInfo t { true, true, 0.0, 120.0 };
    DuckEngine e;
    e.prepare (kRate);
    std::vector<float> gains (1024);
    const int offsets[] = { 100, 100, 700 };   // two notes at once count as one start

    e.process (s, t, nullptr, offsets, 3, gains.data(), 1024);
    CHECK (gains[99] > 0.999f);
    CHECK (gains[100 + 200] < 0.05f);
    CHECK_EQ (e.getTriggerCount(), 2u);
    CHECK (e.getPhase() >= 0.0f && e.getPhase() < 0.1f);

    // Without notes the cycle finishes and the gain returns to 1.
    std::vector<float> more (48000);
    e.process (s, t, nullptr, nullptr, 0, more.data(), 48000);
    CHECK (more.back() > 0.999f);
    CHECK (e.getPhase() < 0.0f);
}

TEST_CASE ("bad host values never produce NaN gains")
{
    DuckSettings s;
    s.rateBeats = std::nan ("");
    s.depth = std::nanf ("");
    TransportInfo t { true, true, std::nan (""), -5.0 };
    DuckEngine e;
    e.prepare (0.0);
    std::vector<float> gains (4096);
    e.process (s, t, nullptr, nullptr, 0, gains.data(), 4096);

    for (float g : gains)
        CHECK (std::isfinite (g) && g >= 0.0f && g <= 1.0f);
}

TEST_CASE ("switching modes mid-curve starts clean")
{
    DuckSettings s;
    s.mode = TriggerMode::Midi;
    TransportInfo t { true, false, 0.0, 120.0 };
    DuckEngine e;
    e.prepare (kRate);
    std::vector<float> gains (4800);
    const int at[] = { 0 };
    e.process (s, t, nullptr, at, 1, gains.data(), 480);
    CHECK (e.getPhase() >= 0.0f);

    s.mode = TriggerMode::Audio;
    e.process (s, t, nullptr, nullptr, 0, gains.data(), 4800);
    CHECK (e.getPhase() < 0.0f);
    CHECK (gains.back() > 0.999f);
}
