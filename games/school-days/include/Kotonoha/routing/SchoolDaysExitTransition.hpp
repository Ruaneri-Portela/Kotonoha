#pragma once

#include <algorithm>
#include <cstdint>

namespace Kotonoha {

// Host timing for the retail System/SYSBASE dialog transition. The game owns
// this policy; the shared renderer only draws the requested texture/opacity.
class SchoolDaysExitTransition final {
public:
    enum class Phase : uint8_t { Idle, Opening, Dialog, Closing, Confirming };
    enum class Target : uint8_t { Quit, Title };
    enum class Event : uint8_t { None, OpenDialog, Closed, Confirmed };

    void Begin(Target target, uint64_t nowMs) {
        target_ = target;
        phase_ = Phase::Opening;
        sinceMs_ = nowMs;
    }

    void Cancel(uint64_t nowMs) {
        if (phase_ != Phase::Dialog) return;
        phase_ = Phase::Closing;
        sinceMs_ = nowMs;
    }

    void Confirm(uint64_t nowMs) {
        if (phase_ != Phase::Dialog) return;
        phase_ = Phase::Confirming;
        sinceMs_ = nowMs;
    }

    Event Update(uint64_t nowMs) {
        switch (phase_) {
        case Phase::Opening:
            if (Elapsed(nowMs) < 500u) return Event::None;
            phase_ = Phase::Dialog;
            return Event::OpenDialog;
        case Phase::Closing:
            if (Elapsed(nowMs) < 500u) return Event::None;
            phase_ = Phase::Idle;
            return Event::Closed;
        case Phase::Confirming:
            if (Elapsed(nowMs) < ConfirmMs()) return Event::None;
            phase_ = Phase::Idle;
            return Event::Confirmed;
        case Phase::Idle:
        case Phase::Dialog:
            return Event::None;
        }
        return Event::None;
    }

    Phase CurrentPhase() const { return phase_; }
    Target CurrentTarget() const { return target_; }
    bool InputLocked() const {
        return phase_ == Phase::Opening || phase_ == Phase::Closing ||
               phase_ == Phase::Confirming;
    }
    bool Active() const { return phase_ != Phase::Idle; }
    uint32_t ConfirmMs() const { return target_ == Target::Quit ? 750u : 500u; }

    uint8_t Alpha(uint64_t nowMs) const {
        const uint64_t elapsed = Elapsed(nowMs);
        if (phase_ == Phase::Opening || phase_ == Phase::Dialog) return 255u;
        if (phase_ == Phase::Closing)
            return static_cast<uint8_t>(255u - std::min<uint64_t>(255u, elapsed * 255u / 500u));
        if (phase_ == Phase::Confirming)
            return static_cast<uint8_t>(std::min<uint64_t>(255u, elapsed * 255u / ConfirmMs()));
        return 0u;
    }

    float RevealedFraction(uint64_t nowMs) const {
        if (phase_ != Phase::Opening) return 1.0f;
        return static_cast<float>(std::min<uint64_t>(500u, Elapsed(nowMs))) / 500.0f;
    }

    void Reset() { phase_ = Phase::Idle; }

private:
    uint64_t Elapsed(uint64_t nowMs) const {
        return nowMs >= sinceMs_ ? nowMs - sinceMs_ : 0u;
    }

    Phase phase_ = Phase::Idle;
    Target target_ = Target::Quit;
    uint64_t sinceMs_ = 0u;
};

} // namespace Kotonoha
