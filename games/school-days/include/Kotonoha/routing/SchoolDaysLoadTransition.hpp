#pragma once

#include <cstdint>

namespace Kotonoha {

// Presentation policy for the retail SaveLoad -> narrative handoff. The
// original first dims LOAD, displays System/SAVELOAD/POPUP_LOAD.PNG, then
// blacks out the message before revealing the restored scene. The observed
// envelopes are measured from v1.02 captures; storage remains synchronous.
class SchoolDaysLoadTransition final {
public:
    enum class Destination : uint8_t { SaveSlot, ReplayData };
    enum class Phase : uint8_t {
        Idle,
        FadeOutPanel,
        ShowLoading,
        FadeOutLoading,
        AwaitRestore,
        RevealScene,
    };
    enum class Event : uint8_t { None, RestoreSlot };

    static constexpr uint32_t PanelFadeMs = 1000u;
    static constexpr uint32_t LoadingHoldMs = 900u;
    static constexpr uint32_t LoadingFadeMs = 500u;
    static constexpr uint32_t SceneRevealMs = 500u;

    bool Begin(int slot, uint64_t nowMs) {
        return BeginFor(Destination::SaveSlot, slot, nowMs);
    }

    bool BeginReplayData(int slot, uint64_t nowMs) {
        return BeginFor(Destination::ReplayData, slot, nowMs);
    }

    bool BeginFor(Destination destination, int slot, uint64_t nowMs) {
        if (Active() || slot < 0) return false;
        slot_ = slot;
        destination_ = destination;
        phase_ = Phase::FadeOutPanel;
        phaseStartMs_ = nowMs;
        return true;
    }

    Event Update(uint64_t nowMs) {
        const uint64_t elapsed = Elapsed(nowMs);
        switch (phase_) {
        case Phase::FadeOutPanel:
            if (elapsed >= PanelFadeMs) SetPhase(Phase::ShowLoading, nowMs);
            break;
        case Phase::ShowLoading:
            if (elapsed >= LoadingHoldMs) SetPhase(Phase::FadeOutLoading, nowMs);
            break;
        case Phase::FadeOutLoading:
            if (elapsed >= LoadingFadeMs) {
                SetPhase(Phase::AwaitRestore, nowMs);
                return Event::RestoreSlot;
            }
            break;
        case Phase::RevealScene:
            if (elapsed >= SceneRevealMs) Reset();
            break;
        case Phase::Idle:
        case Phase::AwaitRestore:
            break;
        }
        return Event::None;
    }

    void Restored(uint64_t nowMs) {
        if (phase_ == Phase::AwaitRestore)
            SetPhase(Phase::RevealScene, nowMs);
    }

    bool Active() const { return phase_ != Phase::Idle; }
    bool InputLocked() const { return Active(); }
    Phase CurrentPhase() const { return phase_; }
    int Slot() const { return slot_; }
    Destination Target() const { return destination_; }

    uint8_t BlackAlpha(uint64_t nowMs) const {
        const auto fraction = [](uint64_t elapsed, uint32_t duration) -> uint8_t {
            return elapsed >= duration ? 255u :
                static_cast<uint8_t>((elapsed * 255u) / duration);
        };
        switch (phase_) {
        case Phase::FadeOutPanel:
            return fraction(Elapsed(nowMs), PanelFadeMs);
        case Phase::FadeOutLoading:
            return fraction(Elapsed(nowMs), LoadingFadeMs);
        case Phase::AwaitRestore:
            return 255u;
        case Phase::RevealScene:
            return static_cast<uint8_t>(255u - fraction(Elapsed(nowMs), SceneRevealMs));
        case Phase::Idle:
        case Phase::ShowLoading:
            return 0u;
        }
        return 0u;
    }

    void Reset() {
        phase_ = Phase::Idle;
        phaseStartMs_ = 0u;
        slot_ = -1;
        destination_ = Destination::SaveSlot;
    }

private:
    uint64_t Elapsed(uint64_t nowMs) const {
        return nowMs >= phaseStartMs_ ? nowMs - phaseStartMs_ : 0u;
    }
    void SetPhase(Phase next, uint64_t nowMs) {
        phase_ = next;
        phaseStartMs_ = nowMs;
    }

    Phase phase_ = Phase::Idle;
    uint64_t phaseStartMs_ = 0u;
    int slot_ = -1;
    Destination destination_ = Destination::SaveSlot;
};

} // namespace Kotonoha
