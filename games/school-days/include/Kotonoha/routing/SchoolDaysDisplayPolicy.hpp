#pragma once

#include <Kotonoha/routing/SchoolDaysSaveFeelModel.hpp>

namespace Kotonoha {

// School Days' four display profiles are logical authoring canvases.  Neither
// the authenticated retail selector nor Kotonoha's remaster fallback changes
// the SDL window/output resolution.
class SchoolDaysDisplayPolicy {
public:
    using Variant = SchoolDaysSaveFeelModel::Variant;

    // Retail v1.02 host state used by SysMenu resource selectors:
    //   effectiveDisplayType: host vtable +0xB8 -> 0x0040EA90
    //   windowMode:           host vtable +0xBC -> 0x0040E830
    //   typeMiniNote:         host vtable +0xC8 -> 0x0040F0D0
    // These are logical/backend compatibility values, not physical output
    // dimensions.
    struct RetailState {
        int effectiveDisplayType = 0;
        int windowMode = 0;
        int typeMiniNote = 0;
    };

    // Minimal mutable retail backend state needed to reproduce Config's
    // authenticated Wide/Full transitions.  B and D are deliberately kept
    // separate: WindowMode selects which one the +0xB8 getter exposes.
    struct RetailBackendState {
        int baseDisplayType = 0;   // B = 0x0050B304
        // D is the selected-device value returned by EXE 0x0040EA90 from the
        // table pointed to by 0x0050B268, indexed by DXDeviceNo 0x0050B2A8.
        // 0x0050B2EC is separate auxiliary state and is not this getter.
        int deviceDisplayType = 0;
        int windowMode = 0;        // W = 0x0050B2C0
        int typeMiniNote = 0;      // M = 0x0050B314
    };

    // Raw host predicates used by Config's accepted-control gates.  Their
    // broader platform meanings are intentionally not inferred here.
    struct RetailConfigInputs {
        int guardCc = 0;      // host vtable +0xCC -> 0x0040F0E0
        int capabilityD0 = 0; // host vtable +0xD0 -> 0x0040F160
        int capabilityD4 = 0; // host vtable +0xD4 -> 0x0040F190
    };

    enum class RetailConfigRequest {
        Wide,
        Full,
    };

    struct Decision {
        Variant variant = Variant::Standard;
        int viewportWidth = 0;
        int viewportHeight = 0;
        float coverage = 0.0f;
    };

    // Exact shipped v1.02 resource-family selector.  Do not normalize the
    // three fields to bools: the retail branches compare DisplayType and
    // WindowMode specifically with 1, and TypeMiniNote specifically with 0.
    static Variant SelectRetailVariant(const RetailState& state);

    // Exact +0xB8 projection used by SysMenu: windowed exposes B, while any
    // nonzero WindowMode exposes the selected-device D value.
    static RetailState ProjectRetailState(const RetailBackendState& state);

    // Apply one decoded Config/Def display request.  actionValue retains the
    // current model's exact control identity: Wide 1/0 == controls 4/5 and Full
    // 1/0 == controls 6/7.  It is NOT a generic desired-state boolean (notably,
    // Full value 1 toggles WindowMode 1->0, while value 0 toggles 0->1).
    // Returns true only when the shipped control gate accepts the request.
    // Accepted Wide requests update B and, when +CC is zero, D; accepted Full
    // requests only toggle WindowMode. No physical window/output state exists.
    static bool ApplyRetailConfigRequest(RetailConfigRequest request,
                                         int actionValue,
                                         const RetailConfigInputs& inputs,
                                         RetailBackendState* state);

    // Canonical logical state for a known authored resource family.  This is
    // only a seed for Kotonoha's explicit/remaster-selected profile; it never
    // requests a physical window size or fullscreen mode.
    static RetailState CanonicalRetailStateForVariant(Variant variant);

    // Kotonoha remaster fallback used only when authenticated retail logical
    // state is unavailable.  This chooses a logical profile by physical output
    // fit; it is intentionally not the retail selection contract.
    static bool SelectAuto(int outputWidth, int outputHeight, Decision* out);
};

} // namespace Kotonoha
