#include <Kotonoha/routing/SchoolDaysTopMeterModel.hpp>
#include <Kotonoha/routing/SchoolDaysTopMeterRenderPlan.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using Meter = Kotonoha::SchoolDaysTopMeterModel;
using RenderPlan = Kotonoha::SchoolDaysTopMeterRenderPlan;

[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysTopMeterModelTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const char* message) {
    if (!condition) Fail(message);
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

void StartChange(Meter& meter, uint32_t captureMs, uint32_t detectMs,
                 int from001, int from002, int to001, int to002,
                 int expectedCode) {
    const auto init = meter.Initialize(from001, from002);
    Check(init.clearFeelingLatch,
          "initial immediate draw did not emit callback30(0) equivalent");
    meter.Tick(captureMs, to001, to002);
    const auto signal = meter.Tick(detectMs, to001, to002);
    Check(meter.CurrentPhase() == Meter::Phase::Interpolate,
          "change did not enter interpolation");
    Check(signal.hostDirectionCode == expectedCode,
          "raw +0x50 direction code mismatch");
}

} // namespace

int main() {
    // FormulaExact / NegativePreserved.
    auto v = Meter::TargetFor(25, 20);
    Check(Near(v.primary, 12.5f) && Near(v.opposite, -12.5f),
          "positive feeling formula mismatch");
    v = Meter::TargetFor(20, 25);
    Check(Near(v.primary, -12.5f) && Near(v.opposite, 12.5f),
          "negative feeling formula was clamped or swapped");
    v = Meter::TargetFor(20, 20);
    Check(Near(v.primary, 0.0f) && Near(v.opposite, 0.0f),
          "neutral feeling formula mismatch");

    // CenterBoundary / IntegerTransition84 / LocalClamp111.
    Check(Meter::Classify(208.5f).geometry == Meter::Geometry::Center &&
              Meter::Classify(-208.5f).geometry == Meter::Geometry::Center,
          "closed center boundary mismatch");
    const float justAboveCenter = std::nextafter(208.5f, INFINITY);
    const float justBelowNegativeCenter = std::nextafter(-208.5f, -INFINITY);
    Check(Meter::Classify(justAboveCenter).geometry == Meter::Geometry::Center &&
              Meter::Classify(justBelowNegativeCenter).geometry ==
                  Meter::Geometry::Center,
          "rounded shifted-component boundary did not match native evaluation order");
    Check(Meter::Classify(210.0f).geometry == Meter::Geometry::PositiveSide &&
              Meter::Classify(-210.0f).geometry == Meter::Geometry::NegativeSide,
          "integer difference 84 did not select side geometry");
    Check(Meter::Classify(207.5f).geometry == Meter::Geometry::Center,
          "integer difference 83 left center geometry");
    Check(Near(Meter::Classify(275.0f).clampedExtent, 483.5f) &&
              Near(Meter::Classify(277.5f).clampedExtent, 485.0f),
          "side-local extent clamp boundary mismatch");

    // Interpolation1500: exact 0/25/50/75%, then snap at >=1500.
    Meter meter;
    StartChange(meter, 10u, 20u, 20, 20, 40, 20, 3);
    const uint32_t start = meter.Timestamp();
    meter.Tick(start + 0u, 40, 20);
    Check(Near(meter.CurrentVisual().primary, 0.0f), "0% interpolation mismatch");
    meter.Tick(start + 375u, 40, 20);
    Check(Near(meter.CurrentVisual().primary, 12.5f), "25% interpolation mismatch");
    meter.Tick(start + 750u, 40, 20);
    Check(Near(meter.CurrentVisual().primary, 25.0f), "50% interpolation mismatch");
    meter.Tick(start + 1125u, 40, 20);
    Check(Near(meter.CurrentVisual().primary, 37.5f), "75% interpolation mismatch");
    meter.Tick(start + 1500u, 40, 20);
    Check(Near(meter.CurrentVisual().primary, 50.0f) &&
              meter.CurrentPhase() == Meter::Phase::PostTarget,
          "1500ms target snap mismatch");

    // The native completion branch takes a second timeGetTime sample after the
    // snap; prove that an advancing second sample starts state3 later.
    StartChange(meter, 30u, 40u, 0, 0, 20, 0, 3);
    const uint32_t sampledStart = meter.Timestamp();
    meter.Tick(sampledStart + 1500u, sampledStart + 1503u, 20, 0);
    Check(meter.CurrentPhase() == Meter::Phase::PostTarget &&
              meter.Timestamp() == sampledStart + 1503u,
          "state2 completion did not store the second native clock sample");

    // PostTargetState2000 / exact state-4 latch clear.
    const uint32_t reached = meter.Timestamp();
    meter.Tick(reached + 1999u, 20, 0);
    Check(meter.CurrentPhase() == Meter::Phase::PostTarget,
          "post-target phase ended before 2000ms");
    meter.Tick(reached + 2000u, 20, 0);
    Check(meter.CurrentPhase() == Meter::Phase::Finalize,
          "post-target phase did not advance at 2000ms");
    const auto finalized = meter.Tick(reached + 2001u, 20, 0);
    Check(meter.CurrentPhase() == Meter::Phase::Capture &&
              finalized.clearFeelingLatch && Near(meter.Previous001(), 20.0f) &&
              Near(meter.Previous002(), 0.0f),
          "state-4 finalize/latch clear mismatch");

    // timeGetTime wrap-around uses unsigned subtraction in the original.
    StartChange(meter, UINT32_C(0xfffffff0), UINT32_C(0xfffffff8),
                0, 0, 20, 0, 3);
    const uint32_t wrapStart = meter.Timestamp();
    meter.Tick(static_cast<uint32_t>(wrapStart + 750u), 20, 0);
    Check(Near(meter.CurrentVisual().primary, 25.0f),
          "timeGetTime wrap-around broke unsigned interpolation elapsed");

    // ChangeDuringActiveCycle: provider changes during state2 and state3 must
    // not retarget or restart the active cycle. The later value is observed
    // only after state4 returns to state0.
    StartChange(meter, 100u, 110u, 0, 0, 20, 0, 3);
    const uint32_t firstStart = meter.Timestamp();
    meter.Tick(firstStart + 750u, 100, 0); // provider changed, must be ignored
    Check(Near(meter.Captured001(), 20.0f) &&
              Near(meter.CurrentVisual().primary, 25.0f),
          "active interpolation retargeted from provider change");
    meter.Tick(firstStart + 1500u, 100, 0);
    const uint32_t firstReached = meter.Timestamp();
    meter.Tick(firstReached + 1000u, 100, 0);
    Check(Near(meter.Captured001(), 20.0f) &&
              Near(meter.CurrentVisual().primary, 50.0f),
          "post-target phase retargeted from provider change");
    meter.Tick(firstReached + 2000u, 100, 0);
    meter.Tick(firstReached + 2001u, 100, 0); // finalize -> state0
    meter.Tick(firstReached + 2002u, 100, 0); // capture provider 100
    Check(Near(meter.Captured001(), 100.0f) &&
              meter.CurrentPhase() == Meter::Phase::Detect,
          "next capture did not observe latest provider value");

    // Decrease path preserves the confirmed raw code 4 without naming it.
    StartChange(meter, 200u, 210u, 40, 20, 10, 20, 4);

    // StateNotClamped: extremely large source values remain raw; only
    // classification derives a locally clamped extent.
    StartChange(meter, 300u, 310u, 0, 0, 1000, 0, 3);
    Check(Near(meter.Captured001(), 1000.0f) &&
              Near(meter.Captured002(), 0.0f),
          "meter model clamped underlying feeling state");

    // The provider conversion occurs before comparisons/deltas. Values that
    // collapse to the same IEEE-754 float above 2^24 must therefore be treated
    // as equal by state1, matching the original x87 load/store fields.
    meter.Initialize(16777216, 0);
    meter.Tick(400u, 16777217, 0);
    const auto roundedEqual = meter.Tick(401u, 16777217, 0);
    Check(meter.CurrentPhase() == Meter::Phase::PostTarget &&
              roundedEqual.hostDirectionCode == 0,
          "provider int->float rounding was not applied before change detection");

    // A non-quarter interpolation witness catches float reassociation drift.
    StartChange(meter, 500u, 510u, 0, 0, -293, 0, 4);
    const uint32_t ulpStart = meter.Timestamp();
    meter.Tick(ulpStart + 1u, -293, 0);
    const float expectedX87Rounded = static_cast<float>(
        (1.0 * -293.0 * 2.5) / 1500.0);
    Check(meter.CurrentVisual().primary == expectedX87Rounded,
          "interpolation operation order differs from x87-equivalent rounding");

    // Native state2 FSTP-rounds the numerator before reloading it for the
    // divide/add. This witness differs from keeping the numerator in extended
    // precision until the final visual store.
    StartChange(meter, 600u, 610u, 0, 0, 2239, 0, 3);
    const uint32_t numeratorRoundStart = meter.Timestamp();
    meter.Tick(numeratorRoundStart + 1499u, 2239, 0);
    constexpr float kNativeRoundedNumeratorPrimary = 5593.76806640625f;
    Check(meter.CurrentVisual().primary == kNativeRoundedNumeratorPrimary &&
              meter.CurrentVisual().opposite ==
                  -kNativeRoundedNumeratorPrimary,
          "state2 numerator FSTP rounding mismatch");

    const auto zeroTarget = Meter::TargetFor(0, 0);
    Check(!std::signbit(zeroTarget.primary) && !std::signbit(zeroTarget.opposite),
          "neutral target did not preserve native positive-zero stores");

    // Authenticated 0x10026540 sprite/crop geometry.  The source rectangles
    // remain in MenuBar_Chip pixel space while only destinations take the
    // logical profile scale.
    auto plan = RenderPlan::Build({0.0f, 0.0f}, 1.0f);
    Check(plan.valid && plan.sprite == RenderPlan::Sprite::Center &&
              Near(plan.source.x, 188.0f) && Near(plan.source.y, 399.0f) &&
              Near(plan.source.w, 417.0f) && Near(plan.source.h, 9.0f) &&
              Near(plan.destination.x, 187.5f) &&
              Near(plan.destination.y, 8.5f) &&
              Near(plan.destination.w, 418.0f) &&
              Near(plan.destination.h, 10.0f),
          "center render-plan geometry mismatch");

    plan = RenderPlan::Build({-210.0f, 210.0f}, 1.0f);
    Check(plan.sprite == RenderPlan::Sprite::NegativeSide &&
              Near(plan.source.x, 67.5f) && Near(plan.source.y, 369.0f) &&
              Near(plan.source.w, 418.5f) &&
              Near(plan.destination.x, 187.5f) &&
              Near(plan.destination.w, 419.5f),
          "negative-side crop/anchor mismatch");

    plan = RenderPlan::Build({210.0f, -210.0f}, 1.0f);
    Check(plan.sprite == RenderPlan::Sprite::PositiveSide &&
              Near(plan.source.x, 1.0f) && Near(plan.source.y, 57.0f) &&
              Near(plan.source.w, 418.5f) &&
              Near(plan.destination.x, 184.0f) &&
              Near(plan.destination.w, 419.5f),
          "positive-side crop/anchor mismatch");

    const auto positiveClamped = RenderPlan::Build({1000.0f, -1000.0f}, 1.0f);
    Check(positiveClamped.sprite == RenderPlan::Sprite::PositiveSide &&
              Near(positiveClamped.source.w, 485.0f) &&
              Near(positiveClamped.destination.x, -606.0f) &&
              Near(positiveClamped.destination.w, 486.0f),
          "positive-side pre-clamp destination offset mismatch");

    const auto fullPlan = RenderPlan::Build({0.0f, 0.0f}, 1.6f);
    const auto notePlan = RenderPlan::Build({0.0f, 0.0f}, 1.28f);
    Check(Near(fullPlan.destination.x, 300.0f) &&
              Near(fullPlan.destination.y, 13.6f) &&
              Near(fullPlan.destination.w, 668.8f) &&
              Near(notePlan.destination.x, 240.0f) &&
              Near(notePlan.destination.w, 535.04f) &&
              Near(fullPlan.source.x, 188.0f) &&
              Near(notePlan.source.x, 188.0f),
          "logical profile scaling leaked into source crop or drifted");

    Check(!RenderPlan::Build({0.0f, 0.0f}, 0.0f).valid,
          "invalid logical scale produced a draw plan");

    std::cout << "SCHOOL DAYS TOP METER GATE 9 PASS\n"
              << "formula_exact=PASS\n"
              << "negative_preserved=PASS\n"
              << "center_boundary=PASS\n"
              << "integer_transition84=PASS\n"
              << "local_clamp111=PASS\n"
              << "state_not_clamped=PASS\n"
              << "interpolation1500=PASS\n"
              << "post_target2000=PASS\n"
              << "change_during_active_cycle=PASS\n"
              << "raw_direction_codes=PASS\n"
              << "render_plan_source_dest=PASS\n"
              << "render_plan_profile_scale=PASS\n";
    return 0;
}
