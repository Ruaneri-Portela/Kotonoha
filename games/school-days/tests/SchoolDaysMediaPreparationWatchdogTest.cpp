#include <Kotonoha/routing/SchoolDaysMediaPreparationWatchdog.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Watchdog = Kotonoha::SchoolDaysMediaPreparationWatchdog;

void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void TestTwoMediaSameTickAndLateCompletion() {
  // WMV and OGG have the same authored ORS START tick, but independent
  // speculative work and independent wall-clock enqueue timestamps.
  struct Media {
    const char *name;
    std::uint32_t startTick;
    Watchdog watch;
    int enqueueCount = 0;
    int dispatchCount = 0;
  };
  std::array<Media, 2> media{{{"05-A2-F00-LXH.wmv", 5915u, {}, 0, 0},
                              {"05-A2-F00-LXH.ogg", 5915u, {}, 0, 0}}};
  constexpr std::uint64_t deadlineMs = 8000u;

  const auto enqueue = [](Media &item, std::uint64_t nowMs) {
    if (!item.watch.Begin(nowMs)) return false;
    ++item.enqueueCount;
    return true;
  };
  Require(media[0].startTick == media[1].startTick,
          "Synthetic LxH movie and audio must share START tick");
  Require(enqueue(media[0], 1000u) && enqueue(media[1], 1000u),
          "Both same-tick resources should enqueue independently");
  Require(!enqueue(media[0], 1001u) && !enqueue(media[1], 1001u),
          "Pending resources were enqueued twice");
  Require(media[0].watch.IsPending() && media[1].watch.IsPending(),
          "Both resources must be pending before results arrive");

  Require(!media[0].watch.Expire(8999u, deadlineMs) &&
              !media[1].watch.Expire(8999u, deadlineMs),
          "Slow-but-valid media was timed out before its full deadline");
  // The OGG finishes just before the deadline; the WMV stays blocked.
  Require(media[1].watch.Complete(), "OGG completion was not accepted");
  Require(!media[1].watch.Expire(9000u, deadlineMs),
          "Completed OGG was incorrectly marked timed out");
  Require(media[0].watch.Expire(9000u, deadlineMs),
          "Blocked WMV did not expire at the exact wall-clock deadline");
  Require(!media[0].watch.Expire(9001u, deadlineMs),
          "A timed-out event expired more than once");
  Require(!media[0].watch.Complete() && !media[0].watch.Fail(),
          "Late worker result revived timed-out WMV");
  Require(!media[1].watch.Complete(),
          "Completed OGG was accepted more than once");

  // Simulate the authored START once for each terminal preparation state.
  for (auto &item : media) {
    if (!item.watch.IsPending()) ++item.dispatchCount;
    Require(!enqueue(item, 9200u),
            "Terminal media event became eligible for requeue");
    Require(item.enqueueCount == 1 && item.dispatchCount == 1,
            "Duplicate enqueue or semantic START occurred");
  }
  Require(media[0].watch.CurrentPhase() == Watchdog::Phase::TimedOut &&
              media[1].watch.CurrentPhase() == Watchdog::Phase::Completed,
          "The two independent media outcomes were mixed");
}

void TestBothMediaExpireWithoutRequeue() {
  Watchdog movie;
  Watchdog se;
  Require(movie.Begin(250u) && se.Begin(250u),
          "Both blocked jobs did not start");
  for (std::uint64_t now : {250u, 8249u}) {
    Require(!movie.Expire(now, 8000u) && !se.Expire(now, 8000u),
            "Expired before the configured deadline");
  }
  Require(movie.Expire(8250u, 8000u) && se.Expire(8250u, 8000u),
          "Same-tick jobs did not independently expire");
  for (std::uint64_t now : {8250u, 9000u, 30000u}) {
    Require(!movie.Expire(now, 8000u) && !se.Expire(now, 8000u),
            "Repeated timeout callback should be impossible");
    Require(!movie.Begin(now) && !se.Begin(now),
            "Timed-out media was requeued");
  }
}

void TestZeroTimestampAndDisabledDeadline() {
  Watchdog atZero;
  Require(atZero.Begin(0u), "Timestamp zero should be a valid enqueue");
  Require(atZero.QueuedAtMs() == 0u && atZero.IsPending(),
          "Zero timestamp was confused with an unqueued event");
  Require(!atZero.Expire(7999u, 8000u) &&
              atZero.Expire(8000u, 8000u),
          "Zero timestamp expiration was incorrect");

  Watchdog disabled;
  Require(disabled.Begin(0u), "Disabled watchdog did not enqueue");
  Require(!disabled.Expire(std::numeric_limits<std::uint64_t>::max(), 0u) &&
              disabled.IsPending(),
          "Deadline zero must disable timeout");
  Require(disabled.Complete() && !disabled.Expire(30000u, 8000u),
          "Disabled watchdog did not permit normal late completion");
}

void TestWrapAndClockRegression() {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto age = Watchdog::ElapsedMs(3u, maximum - 5u);
  Require(age.has_value() && *age == 9u,
          "Unsigned wall clock rollover was not measured correctly");
  Watchdog wrapped;
  Require(wrapped.Begin(maximum - 5u), "Wrapped job did not queue");
  Require(!wrapped.Expire(2u, 9u) && wrapped.Expire(3u, 9u),
          "A genuine monotonic uint64 wrap lost the deadline");

  const auto backward = Watchdog::ElapsedMs(999u, 1000u);
  Require(!backward.has_value() &&
              !Watchdog::DeadlineReached(999u, 1000u, 8000u),
          "A backwards clock step was mistaken for a huge elapsed time");
  Watchdog regressed;
  Require(regressed.Begin(1000u) &&
              !regressed.Expire(999u, 8000u) &&
              regressed.Expire(9000u, 8000u),
          "Clock rollback should not trigger an early timeout");
  Require(!Watchdog::DeadlineReached(maximum, 0u, maximum),
          "Deadline outside the safe half range must fail closed");
}

void TestFailureDefaultBudgetAndSceneReset() {
  Watchdog failed;
  Require(failed.Begin(500u) && failed.Fail(),
          "A failed decode must produce terminal state");
  Require(!failed.Complete() && !failed.Begin(550u) &&
              !failed.Expire(50000u, 8000u),
          "A failed preparation was incorrectly retried");
  failed.Reset();
  Require(failed.CanQueue() && failed.Begin(700u),
          "A NEW scene/generation must permit queueing");
  Require(!failed.Expire(8700u, Watchdog::DefaultDeadlineMs),
          "Default deadline should not prematurely expire slow media");
  Require(!failed.Expire(12699u, Watchdog::DefaultDeadlineMs) &&
              failed.Expire(12700u, Watchdog::DefaultDeadlineMs),
          "Default 12-second deadline was not respected");
}
} // namespace

int main() {
  try {
    TestTwoMediaSameTickAndLateCompletion();
    TestBothMediaExpireWithoutRequeue();
    TestZeroTimestampAndDisabledDeadline();
    TestWrapAndClockRegression();
    TestFailureDefaultBudgetAndSceneReset();
    std::cout << "SchoolDaysMediaPreparationWatchdogTest PASS: paired WMV/OGG, "
                 "completion, timeout, late result, zero, wrap, retry guard\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysMediaPreparationWatchdogTest FAIL: "
              << error.what() << '\n';
    return 1;
  }
}
