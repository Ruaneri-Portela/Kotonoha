#pragma once

#include <algorithm>
#include <cstdint>

namespace Kotonoha {

// Retail gameplay System panel envelope (EXE 0x00426620): black to white
// before opening, then panel to white and white back to gameplay on close.
class SchoolDaysSystemPanelTransition final {
public:
  enum class Target : uint8_t { Save, Load, BackLog, Config };
  enum class Phase : uint8_t { Idle, Opening, Open, Closing, Revealing };
  enum class Event : uint8_t { None, OpenPanel, ClosePanel };

  void Begin(Target target, uint64_t nowMs) {
    target_ = target;
    phase_ = Phase::Opening;
    sinceMs_ = nowMs;
  }

  void BeginClose(uint64_t nowMs) {
    if (phase_ != Phase::Open)
      return;
    phase_ = Phase::Closing;
    sinceMs_ = nowMs;
  }

  Event Update(uint64_t nowMs) {
    if (phase_ == Phase::Opening && Elapsed(nowMs) >= 750u) {
      phase_ = Phase::Open;
      return Event::OpenPanel;
    }
    if (phase_ == Phase::Closing && Elapsed(nowMs) >= 500u) {
      phase_ = Phase::Revealing;
      sinceMs_ = nowMs;
      return Event::ClosePanel;
    }
    if (phase_ == Phase::Revealing && Elapsed(nowMs) >= 500u)
      phase_ = Phase::Idle;
    return Event::None;
  }

  Phase CurrentPhase() const { return phase_; }
  Target CurrentTarget() const { return target_; }
  bool Active() const { return phase_ != Phase::Idle; }
  bool InputLocked() const {
    return phase_ == Phase::Opening || phase_ == Phase::Closing ||
           phase_ == Phase::Revealing;
  }
  bool OpeningFromBlack() const { return phase_ == Phase::Opening; }

  uint8_t WhiteAlpha(uint64_t nowMs) const {
    const uint64_t elapsed = Elapsed(nowMs);
    if (phase_ == Phase::Opening)
      return static_cast<uint8_t>(
          std::min<uint64_t>(255u, elapsed * 255u / 750u));
    if (phase_ == Phase::Closing)
      return static_cast<uint8_t>(
          std::min<uint64_t>(255u, elapsed * 255u / 500u));
    if (phase_ == Phase::Revealing)
      return static_cast<uint8_t>(
          255u - std::min<uint64_t>(255u, elapsed * 255u / 500u));
    return 0u;
  }

  void Reset() { phase_ = Phase::Idle; }

private:
  uint64_t Elapsed(uint64_t nowMs) const {
    return nowMs >= sinceMs_ ? nowMs - sinceMs_ : 0u;
  }

  Phase phase_ = Phase::Idle;
  Target target_ = Target::Save;
  uint64_t sinceMs_ = 0u;
};

} // namespace Kotonoha
