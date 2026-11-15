#pragma once

#include <cstdint>
#include <optional>

namespace Kotonoha {

// Game-owned, wall-clock watchdog for one speculative School Days media event.
// The caller supplies monotonically advancing milliseconds (SDL_GetTicks or
// equivalent), NOT ORS ticks or speed-scaled narrative time. A distinct instance
// must survive for each event ordinal until the scene is retired: erasing a
// pendingMedia entry must not make a timed-out event eligible for requeue.
class SchoolDaysMediaPreparationWatchdog final {
public:
  enum class Phase : std::uint8_t {
    NotQueued,
    Pending,
    Completed,
    Failed,
    TimedOut,
  };

  // Recommended starting budget; production can choose a longer deadline for
  // slow storage. Deadline 0 explicitly disables expiration.
  static constexpr std::uint64_t DefaultDeadlineMs = 12000u;
  static constexpr std::uint64_t HalfClockRangeMs = std::uint64_t{1} << 63;
  static constexpr std::uint64_t MaxDeadlineMs = HalfClockRangeMs - 1u;

  // Unsigned subtraction supports a genuine uint64 wrap. A difference in the
  // upper half of the clock range is ambiguous (e.g. a clock regression), so
  // reject it rather than instantly expiring a newly queued job.
  // Requires less than 2^63 ms between enqueue and observation.
  static std::optional<std::uint64_t> ElapsedMs(
      std::uint64_t nowMs, std::uint64_t queuedAtMs) noexcept {
    const auto age = nowMs - queuedAtMs;
    if (age >= HalfClockRangeMs) return std::nullopt;
    return age;
  }

  static bool DeadlineReached(std::uint64_t nowMs,
                              std::uint64_t queuedAtMs,
                              std::uint64_t deadlineMs) noexcept {
    if (deadlineMs == 0u || deadlineMs > MaxDeadlineMs) return false;
    const auto age = ElapsedMs(nowMs, queuedAtMs);
    return age && *age >= deadlineMs;
  }

  // Begin is deliberately one-shot. Once Completed, Failed or TimedOut, the
  // event cannot be queued again until Reset() on a NEW scene/generation.
  // queuedAtMs == 0 is a valid timestamp, not an "unset" sentinel.
  bool Begin(std::uint64_t queuedAtMs) noexcept {
    if (phase_ != Phase::NotQueued) return false;
    queuedAtMs_ = queuedAtMs;
    phase_ = Phase::Pending;
    return true;
  }

  // Return false for a completion that arrives after timeout/failure.
  // The caller must discard its prepared player/input in that case.
  bool Complete() noexcept { return Finish(Phase::Completed); }
  bool Fail() noexcept { return Finish(Phase::Failed); }

  // Return true only at the first observed expiry. Calling this repeatedly
  // cannot repeat timeout handling or requeue the media event.
  bool Expire(std::uint64_t nowMs, std::uint64_t deadlineMs) noexcept {
    if (phase_ != Phase::Pending ||
        !DeadlineReached(nowMs, queuedAtMs_, deadlineMs))
      return false;
    phase_ = Phase::TimedOut;
    return true;
  }

  bool CanQueue() const noexcept { return phase_ == Phase::NotQueued; }
  bool IsPending() const noexcept { return phase_ == Phase::Pending; }
  Phase CurrentPhase() const noexcept { return phase_; }
  std::uint64_t QueuedAtMs() const noexcept { return queuedAtMs_; }

  std::optional<std::uint64_t> PendingAgeMs(
      std::uint64_t nowMs) const noexcept {
    return IsPending() ? ElapsedMs(nowMs, queuedAtMs_) : std::nullopt;
  }

  // Only the owner that has retired the old ORS event/generation may reset.
  // This helper does not cancel workers or bound a synchronous fallback open.
  void Reset() noexcept {
    phase_ = Phase::NotQueued;
    queuedAtMs_ = 0u;
  }

private:
  bool Finish(Phase finalPhase) noexcept {
    if (phase_ != Phase::Pending) return false;
    phase_ = finalPhase;
    return true;
  }

  Phase phase_ = Phase::NotQueued;
  std::uint64_t queuedAtMs_ = 0u;
};

} // namespace Kotonoha
