#include "core/Curves.h"

#include <algorithm>
#include <cmath>

namespace dk
{
namespace
{
struct CurveEntry
{
    const char* name;
    const char* description;
    CurveSpec spec;
};

// hold, release, recovery, tension
const CurveEntry curves[numCurveShapes] = {
    { "Classic", "The all-round sidechain pump: dips on the kick and swings back up by mid-beat.",
      { 0.00f, 0.60f, Recovery::SCurve, 1.6f } },
    { "Smooth", "A gentle, rounded dip that takes almost the whole beat to come back.",
      { 0.00f, 0.90f, Recovery::SCurve, 1.2f } },
    { "Punchy", "Short and snappy: gets out of the kick's way, then springs straight back.",
      { 0.03f, 0.32f, Recovery::EaseOut, 2.0f } },
    { "Pump", "The big EDM pump: stays down, then rushes back up just before the next kick.",
      { 0.08f, 0.82f, Recovery::Power, 2.2f } },
    { "Tight", "A very short duck, only as long as the kick's click. Subtle on fast rates.",
      { 0.00f, 0.18f, Recovery::EaseOut, 1.5f } },
    { "Long", "Holds the sound down for most of the beat and lets it bloom at the end.",
      { 0.05f, 0.95f, Recovery::Power, 1.4f } },
    { "Bounce", "Quick rebound with a soft landing: lively without sounding chopped.",
      { 0.00f, 0.70f, Recovery::EaseOut, 3.0f } },
    { "Gate", "Hard on/off chop: silent for the first half, full volume for the second.",
      { 0.45f, 0.05f, Recovery::Power, 1.0f } },
    { "Swell", "A straight fade-in over the whole cycle, like a reversed sound.",
      { 0.00f, 1.00f, Recovery::Power, 1.0f } },
    { "Breathe", "Holds briefly, then breathes back in slowly and evenly.",
      { 0.15f, 0.75f, Recovery::SCurve, 2.5f } },
    { "Snap", "Stays down a moment, then snaps back fast. Rhythmic and tight.",
      { 0.22f, 0.14f, Recovery::SCurve, 2.0f } },
    { "Lift", "A light, quick lift that comes back early. Good for vocals and pads.",
      { 0.00f, 0.50f, Recovery::EaseOut, 1.2f } },
};

int indexOf (CurveShape shape) noexcept
{
    return std::clamp ((int) shape, 0, numCurveShapes - 1);
}
} // namespace

const CurveSpec& curveSpec (CurveShape shape)      { return curves[indexOf (shape)].spec; }
const char* curveName (CurveShape shape)           { return curves[indexOf (shape)].name; }
const char* curveDescription (CurveShape shape)    { return curves[indexOf (shape)].description; }

float duckAmount (CurveShape shape, float phase) noexcept
{
    return duckAmount (curveSpec (shape), phase);
}

float duckAmount (const CurveSpec& spec, float phase) noexcept
{
    if (! (phase > 0.0f))   // also catches NaN
        return 1.0f;

    if (phase <= spec.hold)
        return 1.0f;

    if (spec.release <= 0.0f)
        return 0.0f;

    const float u = (phase - spec.hold) / spec.release;

    if (u >= 1.0f)
        return 0.0f;

    const float k = std::max (0.05f, spec.tension);
    float recovered = u;

    switch (spec.recovery)
    {
        case Recovery::Power:
            recovered = std::pow (u, k);
            break;

        case Recovery::EaseOut:
            recovered = 1.0f - std::pow (1.0f - u, k);
            break;

        case Recovery::SCurve:
        {
            const float a = std::pow (u, k), b = std::pow (1.0f - u, k);
            recovered = a / (a + b);
            break;
        }
    }

    return std::clamp (1.0f - recovered, 0.0f, 1.0f);
}
} // namespace dk
