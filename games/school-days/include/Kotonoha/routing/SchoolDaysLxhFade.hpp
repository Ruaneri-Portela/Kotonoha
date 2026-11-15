#pragma once

#include <algorithm>
#include <cstdint>

namespace Kotonoha {

// Remaster-only physical black transitions around an optional LxH epilogue.
// The original EndRoll and authored 24fps ORS fades are not edited. The clock
// is held only during entry OUT and exit OUT. Once the LxH movie starts, the
// first frames are revealed while its normal media clock keeps running.
class SchoolDaysLxhFade final {
public:
  enum class Phase : uint8_t {
    Disabled, AwaitCredits, EntryOut, AwaitMovie, EntryIn,
    Epilogue, ExitOut, ExitIn, Complete
  };
  static constexpr uint32_t DurationMs = 500u;

  void Configure(bool enabled, uint32_t creditsEndTick,
                 uint32_t movieStartTick) {
    creditsEndTick_ = creditsEndTick;
    movieStartTick_ = movieStartTick;
    phase_ = enabled && creditsEndTick > 0u &&
                     movieStartTick >= creditsEndTick
                 ? Phase::AwaitCredits : Phase::Disabled;
    phaseStartMs_ = 0;
  }

  void Reset() { Configure(false, 0u, 0u); }
  bool Enabled() const { return phase_ != Phase::Disabled; }
  Phase CurrentPhase() const { return phase_; }

  // Called BEFORE advancing the authored clock into the EndRoll end tick.
  // That keeps the complete commercial credit sequence intact, including
  // the special ending where the additional movie starts at the same tick.
  bool BeginAtCreditBoundary(uint32_t candidateTick, uint64_t nowMs) {
    if (phase_ != Phase::AwaitCredits || candidateTick < creditsEndTick_)
      return false;
    phase_ = Phase::EntryOut;
    phaseStartMs_ = nowMs;
    return true;
  }

  // Triggered only from the actual extra PLAY_MOVIE event (not EndRoll).
  void MovieStarted(uint32_t movieTick, uint64_t nowMs) {
    if (phase_ == Phase::AwaitMovie && movieTick == movieStartTick_) {
      phase_ = Phase::EntryIn;
      phaseStartMs_ = nowMs;
    }
  }

  // A user seek is not a natural transition from credits: it may intentionally
  // jump past both boundaries. Never leave the game black after such a seek.
  void SeekTo(uint32_t targetTick) {
    if (phase_ == Phase::Disabled || phase_ == Phase::Complete) return;
    phase_ = targetTick >= creditsEndTick_ ? Phase::Epilogue
                                            : Phase::AwaitCredits;
    phaseStartMs_ = 0;
  }

  void Update(uint64_t nowMs) {
    if (Elapsed(nowMs) < DurationMs) return;
    switch (phase_) {
    case Phase::EntryOut: phase_ = Phase::AwaitMovie; break;
    case Phase::EntryIn: phase_ = Phase::Epilogue; break;
    case Phase::ExitIn: phase_ = Phase::Complete; break;
    default: break;
    }
  }

  bool ClockHeld() const { return phase_ == Phase::EntryOut; }
  bool InputLocked() const {
    return phase_ == Phase::EntryOut || phase_ == Phase::AwaitMovie ||
           phase_ == Phase::EntryIn || phase_ == Phase::ExitOut ||
           phase_ == Phase::ExitIn;
  }

  bool StartCountdownExit(uint64_t nowMs) {
    if (!Enabled() || phase_ == Phase::Complete) return false;
    if (phase_ != Phase::ExitOut && phase_ != Phase::ExitIn) {
      phase_ = Phase::ExitOut;
      phaseStartMs_ = nowMs;
    }
    return true;
  }

  bool ExitReady(uint64_t nowMs) const {
    return phase_ == Phase::ExitOut && Elapsed(nowMs) >= DurationMs;
  }

  void CountdownOpened(uint64_t nowMs) {
    if (phase_ == Phase::ExitOut) {
      phase_ = Phase::ExitIn;
      phaseStartMs_ = nowMs;
    }
  }

  bool HoldCountdown() const { return phase_ == Phase::ExitIn; }

  uint8_t BlackAlpha(uint64_t nowMs) const {
    switch (phase_) {
    case Phase::EntryOut:
    case Phase::ExitOut:
      return static_cast<uint8_t>(std::min<uint64_t>(
          255u, Elapsed(nowMs) * 255u / DurationMs));
    case Phase::AwaitMovie: return 255u;
    case Phase::EntryIn:
    case Phase::ExitIn:
      return static_cast<uint8_t>(255u - std::min<uint64_t>(
          255u, Elapsed(nowMs) * 255u / DurationMs));
    default: return 0u;
    }
  }

private:
  uint64_t Elapsed(uint64_t nowMs) const {
    return nowMs >= phaseStartMs_ ? nowMs - phaseStartMs_ : 0u;
  }
  uint32_t creditsEndTick_ = 0;
  uint32_t movieStartTick_ = 0;
  Phase phase_ = Phase::Disabled;
  uint64_t phaseStartMs_ = 0;
};

} // namespace Kotonoha
