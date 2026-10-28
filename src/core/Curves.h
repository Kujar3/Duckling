#pragma once

// Duck curves: how far the volume is pulled down over one duck cycle. Pure C++17, no JUCE.
//
// A curve maps the cycle phase (0 = the trigger / the beat, 1 = the end of the cycle) to a duck amount: 1 = fully
// ducked (the Depth knob decides how far that is), 0 = untouched. Every curve starts fully ducked, holds for `hold`,
// recovers over `release` along one of three recovery shapes and is fully recovered at or before phase 1.

namespace dk
{
enum class CurveShape
{
    Classic = 0,   // values are stored in projects: only ever append before NumCurves
    Smooth,
    Punchy,
    Pump,
    Tight,
    Long,
    Bounce,
    Gate,
    Swell,
    Breathe,
    Snap,
    Lift,
    NumCurves
};

constexpr int numCurveShapes = (int) CurveShape::NumCurves;

enum class Recovery
{
    Power,     // r = u^k: starts slow, finishes fast (k > 1); linear for k = 1
    EaseOut,   // r = 1 - (1 - u)^k: comes back fast, settles slowly
    SCurve     // r = u^k / (u^k + (1 - u)^k): slow - fast - slow
};

struct CurveSpec
{
    float hold = 0.0f;       // phase fraction held fully ducked
    float release = 0.5f;    // phase fraction spent recovering (hold + release <= 1)
    Recovery recovery = Recovery::SCurve;
    float tension = 1.0f;    // k above, > 0
};

const CurveSpec& curveSpec (CurveShape);
const char* curveName (CurveShape);          // "Classic", "Smooth", ...
const char* curveDescription (CurveShape);   // one short sentence for tooltips

// Duck amount 0..1 at `phase` (clamped to [0, 1]). 1 at phase 0, non-increasing, 0 from hold + release on.
float duckAmount (CurveShape, float phase) noexcept;
float duckAmount (const CurveSpec&, float phase) noexcept;
} // namespace dk
