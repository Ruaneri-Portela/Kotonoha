#include <Kotonoha/routing/SchoolDaysTopMeterModel.hpp>

#include <algorithm>

namespace Kotonoha {

SchoolDaysTopMeterModel::Visual SchoolDaysTopMeterModel::TargetFor(
    int feeling001, int feeling002) {
    return TargetForFloats(static_cast<float>(feeling001),
                           static_cast<float>(feeling002));
}

SchoolDaysTopMeterModel::Visual SchoolDaysTopMeterModel::TargetForFloats(
    float feeling001, float feeling002) {
    // Native x87 loads the already-rounded float fields, then performs the
    // subtract/multiply in extended precision before storing float. Double is
    // sufficient to preserve the exact float inputs and avoids the extra
    // reassociation/rounding of a pure-float expression on the x64 port.
    const double primaryExtended =
        (static_cast<double>(feeling001) - static_cast<double>(feeling002)) *
        2.5;
    const double oppositeExtended =
        (static_cast<double>(feeling002) - static_cast<double>(feeling001)) *
        2.5;
    const float primary = static_cast<float>(primaryExtended);
    const float opposite = static_cast<float>(oppositeExtended);
    return {primary, opposite};
}

SchoolDaysTopMeterModel::Visual SchoolDaysTopMeterModel::CapturedTarget() const {
    return TargetForFloats(target001, target002);
}

SchoolDaysTopMeterModel::GeometryState SchoolDaysTopMeterModel::Classify(
    float primaryVisual) {
    Visual visual;
    visual.primary = primaryVisual;
    visual.opposite = -primaryVisual;
    return Classify(visual);
}

SchoolDaysTopMeterModel::GeometryState SchoolDaysTopMeterModel::Classify(
    const Visual& visual) {
    GeometryState out;
    // 0x10026540 stores `component + 208.5` to a float local first and only
    // then compares that rounded value against 417.0. Comparing the unshifted
    // component directly against 208.5 is not bit-equivalent at the boundary.
    const float primaryShifted = visual.primary + kCenterHalfSpan;
    const float oppositeShifted = visual.opposite + kCenterHalfSpan;
    if (oppositeShifted > 417.0f) {
        out.geometry = Geometry::NegativeSide;
        out.shiftedComponent = oppositeShifted;
    }
    else if (primaryShifted > 417.0f) {
        out.geometry = Geometry::PositiveSide;
        out.shiftedComponent = primaryShifted;
    }
    else {
        out.geometry = Geometry::Center;
        out.shiftedComponent = primaryShifted;
    }
    out.clampedExtent =
        std::clamp(out.shiftedComponent, 0.0f, kSideExtentMax);
    return out;
}

SchoolDaysTopMeterModel::TickResult SchoolDaysTopMeterModel::Initialize(
    int feeling001, int feeling002) {
    previous001 = static_cast<float>(feeling001);
    previous002 = static_cast<float>(feeling002);
    target001 = previous001;
    target002 = previous002;
    delta001 = 0.0f;
    delta002 = 0.0f;
    visual = TargetForFloats(previous001, previous002);
    visualStart = visual;
    timestamp = 0u;
    phase = Phase::Capture;
    TickResult result;
    result.clearFeelingLatch = true;
    return result;
}

SchoolDaysTopMeterModel::TickResult SchoolDaysTopMeterModel::Tick(
    uint32_t nowMs, int current001, int current002) {
    return Tick(nowMs, nowMs, current001, current002);
}

SchoolDaysTopMeterModel::TickResult SchoolDaysTopMeterModel::Tick(
    uint32_t nowMs, uint32_t postInterpolationNowMs,
    int current001, int current002) {
    TickResult result;

    switch (phase) {
    case Phase::Capture:
        // 0x10026B40 reads 002 first and 001 second, but stores them in these
        // logical destinations; order has no externally visible effect here.
        target001 = static_cast<float>(current001);
        target002 = static_cast<float>(current002);
        visualStart = visual;
        delta001 = target001 - previous001;
        delta002 = target002 - previous002;
        phase = Phase::Detect;
        break;

    case Phase::Detect:
        if (previous001 == target001 && previous002 == target002) {
            // Exact state-1 fast path: no new timestamp and no +0x50 code.
            phase = Phase::PostTarget;
            break;
        }

        timestamp = nowMs;
        phase = Phase::Interpolate;
        // The binary picks the first changed raw feeling in this order. Code 4
        // is emitted for a non-increase; code 3 for an increase. Their external
        // semantic meaning is intentionally not named here.
        if (previous001 != target001) {
            result.hostDirectionCode = target001 <= previous001 ? 4 : 3;
        }
        else {
            result.hostDirectionCode = target002 <= previous002 ? 4 : 3;
        }
        break;

    case Phase::Interpolate: {
        const uint32_t elapsed = nowMs - timestamp; // timeGetTime-style wrap
        if (elapsed < kInterpolationMs) {
            // Preserve the native operation order. State 2 computes each
            // direction's elapsed * delta-difference * 2.5 numerator in x87
            // extended precision, FSTP-rounds that numerator to float, then
            // reloads it for / 1500 + visualStart before the final float store.
            const double elapsedExtended = static_cast<double>(elapsed);
            const double primaryDeltaExtended =
                static_cast<double>(delta001) - static_cast<double>(delta002);
            const double oppositeDeltaExtended =
                static_cast<double>(delta002) - static_cast<double>(delta001);
            const float primaryNumerator = static_cast<float>(
                elapsedExtended * primaryDeltaExtended * 2.5);
            const float oppositeNumerator = static_cast<float>(
                elapsedExtended * oppositeDeltaExtended * 2.5);
            visual.primary = static_cast<float>(
                static_cast<double>(primaryNumerator) / 1500.0 +
                static_cast<double>(visualStart.primary));
            visual.opposite = static_cast<float>(
                static_cast<double>(oppositeNumerator) / 1500.0 +
                static_cast<double>(visualStart.opposite));
        }
        else {
            visual = TargetForFloats(target001, target002);
            timestamp = postInterpolationNowMs;
            phase = Phase::PostTarget;
        }
        break;
    }

    case Phase::PostTarget:
        // Original condition is `1999 < elapsed`, i.e. advance at >=2000 ms.
        if ((nowMs - timestamp) >= kPostTargetMs) {
            phase = Phase::Finalize;
        }
        break;

    case Phase::Finalize:
        timestamp = 0u;
        phase = Phase::Capture;
        previous001 = target001;
        previous002 = target002;
        result.clearFeelingLatch = true;
        break;
    }
    return result;
}

} // namespace Kotonoha
