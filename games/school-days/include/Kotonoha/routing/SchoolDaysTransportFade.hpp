#pragma once

#include <algorithm>
#include <cstdint>

namespace Kotonoha {

// Retail FILMEngine state 4, SysMenuSDHQ control 2/3 -> EXE 0x00425BF0:
// the scene must fade to black BEFORE the rewind/skip clock reconstruction.
// OUT=500 ms at EXE 0x00425CBF..0x00425CDB; after state4 substate5 marks
// +0x563, normal tick 0x00425351..0x00425374 issues IN=1000 ms. The latter
// starts only after the seek/rebuild phase, avoiding abrupt black flashes.
class SchoolDaysTransportFade final {
public:
  enum class Operation : uint8_t { Rewind, SkipToEnd };
  enum class Phase : uint8_t {
    Idle,
    FadeOut,
    AwaitNavigation,
    AwaitReady,
    FadeIn
  };
  enum class Event : uint8_t { None, Navigate };

  static constexpr uint32_t FadeOutMs = 500u;
  static constexpr uint32_t FadeInMs = 1000u;

  bool Begin(Operation operation, uint64_t nowMs) {
    if (Active())
      return false;
    operation_ = operation;
    phase_ = Phase::FadeOut;
    phaseStartMs_ = nowMs;
    return true;
  }

  Event Update(uint64_t nowMs) {
    if (phase_ == Phase::FadeOut && Elapsed(nowMs) >= FadeOutMs) {
      phase_ = Phase::AwaitNavigation;
      return Event::Navigate;
    }
    if (phase_ == Phase::FadeIn && Elapsed(nowMs) >= FadeInMs)
      Reset();
    return Event::None;
  }

  void Navigated() {
    if (phase_ == Phase::AwaitNavigation)
      phase_ = Phase::AwaitReady;
  }

  void Reveal(uint64_t nowMs) {
    if (phase_ != Phase::AwaitReady)
      return;
    phase_ = Phase::FadeIn;
    phaseStartMs_ = nowMs;
  }

  bool Active() const { return phase_ != Phase::Idle; }
  bool InputLocked() const { return Active(); }
  Phase CurrentPhase() const { return phase_; }
  Operation CurrentOperation() const { return operation_; }

  uint8_t BlackAlpha(uint64_t nowMs) const {
    const uint64_t elapsed = Elapsed(nowMs);
    const auto fraction = [](uint64_t value, uint32_t duration) {
      return static_cast<uint8_t>(
          std::min<uint64_t>(255u, value * 255u / duration));
    };
    switch (phase_) {
    case Phase::FadeOut:
      return fraction(elapsed, FadeOutMs);
    case Phase::AwaitNavigation:
    case Phase::AwaitReady:
      return 255u;
    case Phase::FadeIn:
      return static_cast<uint8_t>(255u - fraction(elapsed, FadeInMs));
    case Phase::Idle:
      return 0u;
    }
    return 0u;
  }

  void Reset() {
    phase_ = Phase::Idle;
    phaseStartMs_ = 0;
  }

private:
  uint64_t Elapsed(uint64_t nowMs) const {
    return nowMs >= phaseStartMs_ ? nowMs - phaseStartMs_ : 0u;
  }

  Operation operation_ = Operation::Rewind;
  Phase phase_ = Phase::Idle;
  uint64_t phaseStartMs_ = 0u;
};

} // namespace Kotonoha
