#include <Kotonoha/routing/SchoolDaysDisplayPolicy.hpp>

#include <algorithm>
#include <cmath>

namespace Kotonoha {
namespace {

struct Fit {
    int width = 0;
    int height = 0;
    float coverage = 0.0f;
};

Fit FitLogical(int outputWidth, int outputHeight, int logicalWidth,
               int logicalHeight) {
    Fit fit;
    if (outputWidth <= 0 || outputHeight <= 0 || logicalWidth <= 0 ||
        logicalHeight <= 0) {
        return fit;
    }
    const double scale = std::min(
        static_cast<double>(outputWidth) / static_cast<double>(logicalWidth),
        static_cast<double>(outputHeight) / static_cast<double>(logicalHeight));
    fit.width = std::max(1, static_cast<int>(std::floor(logicalWidth * scale + 0.5)));
    fit.height = std::max(1, static_cast<int>(std::floor(logicalHeight * scale + 0.5)));
    const double outputArea = static_cast<double>(outputWidth) * outputHeight;
    fit.coverage = static_cast<float>(
        (static_cast<double>(fit.width) * fit.height) / outputArea);
    return fit;
}

} // namespace

SchoolDaysDisplayPolicy::Variant SchoolDaysDisplayPolicy::SelectRetailVariant(
    const RetailState& state) {
    // SysMenu v1.02 uses the same gate for its four resource families:
    //   DisplayType != 1                         -> Standard
    //   DisplayType == 1 && WindowMode != 1     -> Wide
    //   DisplayType == 1 && WindowMode == 1 &&
    //       TypeMiniNote == 0                    -> WideFull
    //       TypeMiniNote != 0                    -> WideNote
    if (state.effectiveDisplayType != 1) return Variant::Standard;
    if (state.windowMode != 1) return Variant::Wide;
    return state.typeMiniNote == 0 ? Variant::WideFull : Variant::WideNote;
}

SchoolDaysDisplayPolicy::RetailState SchoolDaysDisplayPolicy::ProjectRetailState(
    const RetailBackendState& state) {
    return {state.windowMode == 0 ? state.baseDisplayType
                                  : state.deviceDisplayType,
            state.windowMode, state.typeMiniNote};
}

bool SchoolDaysDisplayPolicy::ApplyRetailConfigRequest(
    RetailConfigRequest request, int actionValue,
    const RetailConfigInputs& inputs, RetailBackendState* state) {
    if (state == nullptr) return false;
    if (actionValue != 0 && actionValue != 1) return false;

    const RetailState projected = ProjectRetailState(*state);
    if (request == RetailConfigRequest::Wide) {
        // Config control 4 is accepted only from effective DisplayType 0;
        // control 5 only from effective DisplayType 1.  Both merely arm the
        // same Wide request latch; the EXE consumer performs the transition.
        const int targetDisplayType = actionValue;
        const int requiredCurrent = actionValue == 1 ? 0 : 1;
        if (projected.effectiveDisplayType != requiredCurrent) return false;

        // +CC nonzero accepts Wide only in WindowMode 0.  With +CC zero the
        // +D0 capability must be nonzero.  The consumer always updates B; D
        // follows the same target only on the +CC==0 path.
        if (inputs.guardCc != 0) {
            if (state->windowMode != 0) return false;
        } else if (inputs.capabilityD0 == 0) {
            return false;
        }

        state->baseDisplayType = targetDisplayType;
        if (inputs.guardCc == 0) {
            state->deviceDisplayType = targetDisplayType;
        }
        return true;
    }

    if (request == RetailConfigRequest::Full) {
        // Config's Full pair is visually/action-value inverted relative to
        // WindowMode: control 6 is accepted from W==1 and toggles to 0;
        // control 7 is accepted from W==0 and toggles to 1.
        const int requiredCurrent = actionValue == 1 ? 1 : 0;
        if (state->windowMode != requiredCurrent || inputs.capabilityD4 == 0) {
            return false;
        }
        state->windowMode = state->windowMode == 0 ? 1 : 0;
        return true;
    }

    return false;
}

SchoolDaysDisplayPolicy::RetailState
SchoolDaysDisplayPolicy::CanonicalRetailStateForVariant(Variant variant) {
    switch (variant) {
    case Variant::Standard:
        return {0, 0, 0};
    case Variant::Wide:
        return {1, 0, 0};
    case Variant::WideFull:
        return {1, 1, 0};
    case Variant::WideNote:
        return {1, 1, 1};
    }
    return {0, 0, 0};
}

bool SchoolDaysDisplayPolicy::SelectAuto(int outputWidth, int outputHeight,
                                         Decision* out) {
    if (out == nullptr || outputWidth <= 0 || outputHeight <= 0) return false;
    *out = {};

    const Fit standard = FitLogical(outputWidth, outputHeight, 800, 600);
    const Fit wide = FitLogical(outputWidth, outputHeight, 1280, 720);

    // Pick the aspect family that occupies more of the physical output.  This
    // naturally keeps 4:3/5:4 displays on Standard while 16:10, 16:9 and
    // ultrawide outputs use the wide assets with pillarboxing where necessary.
    if (standard.coverage > wide.coverage) {
        out->variant = Variant::Standard;
        out->viewportWidth = standard.width;
        out->viewportHeight = standard.height;
        out->coverage = standard.coverage;
        return true;
    }

    out->viewportWidth = wide.width;
    out->viewportHeight = wide.height;
    out->coverage = wide.coverage;

    // The three wide variants share 16:9 composition but carry different
    // authored logical densities. Choose the densest profile that fits in the
    // fitted 16:9 viewport; subsequent rendering may still scale it up/down.
    if (wide.width >= 1280 && wide.height >= 720) {
        out->variant = Variant::WideFull;
    } else if (wide.width >= 1024 && wide.height >= 576) {
        out->variant = Variant::WideNote;
    } else {
        out->variant = Variant::Wide;
    }
    return true;
}

} // namespace Kotonoha
