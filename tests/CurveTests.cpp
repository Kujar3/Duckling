#include "TestFramework.h"

#include "core/Curves.h"

#include <cmath>
#include <cstring>
#include <set>
#include <string>

using namespace dk;

TEST_CASE ("every curve starts fully ducked and ends recovered")
{
    for (int c = 0; c < numCurveShapes; ++c)
    {
        const auto shape = (CurveShape) c;
        CHECK_EQ (duckAmount (shape, 0.0f), 1.0f);
        CHECK_EQ (duckAmount (shape, 1.0f), 0.0f);
        CHECK (duckAmount (shape, 0.999f) < 0.02f);

        const auto& spec = curveSpec (shape);
        CHECK (spec.hold >= 0.0f);
        CHECK (spec.release > 0.0f);
        CHECK (spec.hold + spec.release <= 1.0f + 1.0e-6f);
        CHECK (spec.tension > 0.0f);
    }
}

TEST_CASE ("curves never rise back into the duck (non-increasing, within 0..1)")
{
    for (int c = 0; c < numCurveShapes; ++c)
    {
        float previous = 1.0f;

        for (int i = 0; i <= 2000; ++i)
        {
            const float v = duckAmount ((CurveShape) c, (float) i / 2000.0f);
            CHECK (v >= 0.0f && v <= 1.0f);
            CHECK (v <= previous + 1.0e-6f);
            previous = v;
        }
    }
}

TEST_CASE ("hold keeps the full duck, release reaches zero")
{
    const CurveSpec spec { 0.25f, 0.5f, Recovery::Power, 1.0f };
    CHECK_EQ (duckAmount (spec, 0.2f), 1.0f);
    CHECK_EQ (duckAmount (spec, 0.25f), 1.0f);
    CHECK (std::abs (duckAmount (spec, 0.5f) - 0.5f) < 1.0e-5f);   // linear: halfway through the release
    CHECK_EQ (duckAmount (spec, 0.75f), 0.0f);
    CHECK_EQ (duckAmount (spec, 0.9f), 0.0f);
}

TEST_CASE ("recovery shapes have their character")
{
    const CurveSpec power { 0.0f, 1.0f, Recovery::Power, 2.0f };
    const CurveSpec easeOut { 0.0f, 1.0f, Recovery::EaseOut, 2.0f };
    const CurveSpec scurve { 0.0f, 1.0f, Recovery::SCurve, 2.0f };

    CHECK (duckAmount (power, 0.5f) > 0.7f);     // still mostly ducked half way
    CHECK (duckAmount (easeOut, 0.5f) < 0.3f);   // mostly back half way
    CHECK (std::abs (duckAmount (scurve, 0.5f) - 0.5f) < 1.0e-5f);
}

TEST_CASE ("out-of-range and NaN phases are safe")
{
    CHECK_EQ (duckAmount (CurveShape::Classic, -0.5f), 1.0f);
    CHECK_EQ (duckAmount (CurveShape::Classic, 3.0f), 0.0f);
    CHECK_EQ (duckAmount (CurveShape::Classic, std::nanf ("")), 1.0f);
    CHECK_EQ (std::string (curveName ((CurveShape) 999)), std::string (curveName (CurveShape::Lift)));
}

TEST_CASE ("curve names are unique and every curve has a description")
{
    std::set<std::string> names;

    for (int c = 0; c < numCurveShapes; ++c)
    {
        names.insert (curveName ((CurveShape) c));
        CHECK (std::strlen (curveDescription ((CurveShape) c)) > 20);
    }

    CHECK_EQ ((int) names.size(), numCurveShapes);
}
