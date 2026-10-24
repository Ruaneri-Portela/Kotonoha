#pragma once

#include <algorithm>
#include <cstdint>

namespace Kotonoha {

// Pure timing policy for the authenticated v1.02 frontend fades driven by
// EXE 0x0041EF60.  Alpha is integer-linear, matching elapsed*255/duration.
class SchoolDaysFrontendTransition final {
public:
    enum class Color : uint8_t { Black, White };
    enum class Profile : uint8_t {
        TitleToStart,
        TitleToChild,
        ChildToTitle,
    };
    enum class Phase : uint8_t { Idle, Outgoing, AwaitHandoff, Incoming };

    struct Spec {
        uint32_t outgoingMs = 0;
        Color outgoingColor = Color::Black;
        uint32_t incomingMs = 0;
        Color incomingColor = Color::Black;
    };

    static constexpr Spec SpecFor(Profile profile) {
        switch (profile) {
        case Profile::TitleToStart:
            return {1000u, Color::Black, 0u, Color::Black};
        case Profile::TitleToChild:
            return {500u, Color::White, 500u, Color::White};
        case Profile::ChildToTitle:
            return {500u, Color::White, 500u, Color::White};
        }
        return {};
    }

    static constexpr uint8_t OutgoingAlpha(uint64_t elapsed,
                                            uint32_t duration) {
        if (duration == 0u || elapsed >= duration) return 255u;
        return static_cast<uint8_t>((elapsed * 255u) / duration);
    }

    static constexpr uint8_t IncomingAlpha(uint64_t elapsed,
                                            uint32_t duration) {
        if (duration == 0u || elapsed >= duration) return 0u;
        return static_cast<uint8_t>(255u - ((elapsed * 255u) / duration));
    }

    void Begin(Profile profile, uint64_t nowMs) {
        spec_ = SpecFor(profile);
        phase_ = spec_.outgoingMs == 0u ? Phase::AwaitHandoff : Phase::Outgoing;
        phaseStartMs_ = nowMs;
    }

    void BeginIncoming(Color color, uint32_t durationMs, uint64_t nowMs) {
        spec_ = {0u, color, durationMs, color};
        phase_ = durationMs == 0u ? Phase::Idle : Phase::Incoming;
        phaseStartMs_ = nowMs;
    }

    void Update(uint64_t nowMs) {
        if (phase_ == Phase::Outgoing && Elapsed(nowMs) >= spec_.outgoingMs)
            phase_ = Phase::AwaitHandoff;
        if (phase_ == Phase::Incoming && Elapsed(nowMs) >= spec_.incomingMs)
            phase_ = Phase::Idle;
    }

    bool NeedsHandoff(uint64_t nowMs) {
        Update(nowMs);
        return phase_ == Phase::AwaitHandoff;
    }

    void CompleteHandoff(uint64_t nowMs) {
        if (phase_ != Phase::AwaitHandoff) return;
        if (spec_.incomingMs == 0u) {
            phase_ = Phase::Idle;
        } else {
            phase_ = Phase::Incoming;
            phaseStartMs_ = nowMs;
        }
    }

    bool Active(uint64_t nowMs) {
        Update(nowMs);
        return phase_ != Phase::Idle;
    }

    Phase CurrentPhase(uint64_t nowMs) {
        Update(nowMs);
        return phase_;
    }

    uint8_t Alpha(uint64_t nowMs) {
        Update(nowMs);
        switch (phase_) {
        case Phase::Outgoing:
            return OutgoingAlpha(Elapsed(nowMs), spec_.outgoingMs);
        case Phase::AwaitHandoff:
            return 255u;
        case Phase::Incoming:
            return IncomingAlpha(Elapsed(nowMs), spec_.incomingMs);
        case Phase::Idle:
            return 0u;
        }
        return 0u;
    }

    Color CurrentColor() const {
        return phase_ == Phase::Incoming ? spec_.incomingColor
                                         : spec_.outgoingColor;
    }

    void Reset() {
        phase_ = Phase::Idle;
        phaseStartMs_ = 0u;
        spec_ = {};
    }

private:
    uint64_t Elapsed(uint64_t nowMs) const {
        return nowMs >= phaseStartMs_ ? nowMs - phaseStartMs_ : 0u;
    }

    Spec spec_{};
    Phase phase_ = Phase::Idle;
    uint64_t phaseStartMs_ = 0u;
};

} // namespace Kotonoha
