#pragma once

#include <cstdint>

namespace Kotonoha {

// Pure behavioral projection of SysMenuSDHQ.dll FILM::MenuBar's confirmed
// feeling-meter state machine (0x10026050 / 0x10026B40). It intentionally
// contains no renderer, character-side naming, or interpretation of the host
// +0x50 codes 3/4.
class SchoolDaysTopMeterModel {
public:
    enum class Phase : uint8_t {
        Capture = 0,
        Detect = 1,
        Interpolate = 2,
        PostTarget = 3,
        Finalize = 4,
    };

    enum class Geometry : uint8_t {
        NegativeSide,
        Center,
        PositiveSide,
    };

    struct Visual {
        float primary = 0.0f;  // original this+0x48
        float opposite = 0.0f; // original this+0x4C
    };

    struct TickResult {
        // Raw, confirmed host +0x50 code emitted on Detect when animation
        // starts. Zero means no such call this tick. Semantic names for 3/4
        // remain unknown in the authenticated evidence.
        int hostDirectionCode = 0;
        // Mirrors interface +0x30(0) in state 4.
        bool clearFeelingLatch = false;
    };

    struct GeometryState {
        Geometry geometry = Geometry::Center;
        // component + 208.5 used by the side/center decision.
        float shiftedComponent = 208.5f;
        // Side-local clamp only. Do not interpret this as full visual
        // saturation; the positive-side original keeps a pre-clamp argument.
        float clampedExtent = 208.5f;
    };

    static constexpr float kScale = 2.5f;
    static constexpr float kCenterHalfSpan = 208.5f;
    static constexpr float kSideExtentMax = 485.0f;
    static constexpr uint32_t kInterpolationMs = 1500u;
    static constexpr uint32_t kPostTargetMs = 2000u;

    // 0x10026050 also emits interface +0x30(0) after its immediate initial
    // draw, represented by clearFeelingLatch in the returned result.
    TickResult Initialize(int feeling001, int feeling002);

    // current001/current002 represent the provider values available at this
    // outer tick. They are consulted only in Phase::Capture, exactly like the
    // original machine; changes during Interpolate/PostTarget are not queued or
    // used until a later Capture pass.
    TickResult Tick(uint32_t nowMs, int current001, int current002);
    // Exact state-2 completion seam: the native routine samples timeGetTime()
    // once for elapsed, then samples it again after snapping the target and
    // stores that second value as the state-3 timestamp. Callers that can
    // observe two samples may use this overload; the 3-argument form coalesces
    // both samples to nowMs for ordinary frame-driven use.
    TickResult Tick(uint32_t nowMs, uint32_t postInterpolationNowMs,
                    int current001, int current002);

    static Visual TargetFor(int feeling001, int feeling002);
    static GeometryState Classify(float primaryVisual);
    static GeometryState Classify(const Visual& visual);

    Phase CurrentPhase() const { return phase; }
    Visual CurrentVisual() const { return visual; }
    Visual CapturedTarget() const;
    float Previous001() const { return previous001; }
    float Previous002() const { return previous002; }
    float Captured001() const { return target001; }
    float Captured002() const { return target002; }
    uint32_t Timestamp() const { return timestamp; }

private:
    Phase phase = Phase::Capture;
    // The native provider returns ints, but both 0x10026050 and state 0 of
    // 0x10026B40 convert them to float before storing/comparing/subtracting.
    float previous001 = 0.0f;
    float previous002 = 0.0f;
    float target001 = 0.0f;
    float target002 = 0.0f;
    float delta001 = 0.0f;
    float delta002 = 0.0f;
    Visual visual{};
    Visual visualStart{};
    uint32_t timestamp = 0u;

    static Visual TargetForFloats(float feeling001, float feeling002);
};

} // namespace Kotonoha
