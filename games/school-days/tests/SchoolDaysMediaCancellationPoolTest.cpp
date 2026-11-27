#include <Kotonoha/routing/SchoolDaysMediaCancellationPool.hpp>

#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
using Pool = Kotonoha::SchoolDaysMediaCancellationPool;

void Require(bool condition, const char* explanation) {
  if (!condition) throw std::runtime_error(explanation);
}

void TestDiscardingOnePreparedInputDoesNotCancelOthers() {
  Pool scene;
  // In the real decoder, State::~State() calls token->store(true) when a
  // speculative PreparedInput is dropped. This must NOT cancel unrelated
  // WMV and OGG jobs, including two with the exact same authored start tick.
  const auto earlierVoice = scene.Allocate();
  const auto movie = scene.Allocate();
  const auto sameTickAudio = scene.Allocate();
  Require(!earlierVoice->load() && !movie->load() && !sameTickAudio->load(),
          "Initial media token was cancelled");
  earlierVoice->store(true);
  Require(!movie->load() && !sameTickAudio->load(),
          "Discarding old voice cancelled both LxH same-tick resources");
  movie->store(true);
  Require(!sameTickAudio->load(),
          "Dropping completed WMV incorrectly cancelled its OGG companion");
  const auto after = scene.Allocate();
  Require(!after->load(), "A new job inherited another media token's state");
  scene.CancelAll();
  Require(earlierVoice->load() && movie->load() && sameTickAudio->load() &&
              after->load(),
          "Reset did not cancel every still-live media job");
  const auto nextGeneration = scene.Allocate();
  Require(!nextGeneration->load(),
          "A new scene inherited the cancelled generation");
}

void TestSeparateBgmIntroAndLoop() {
  Pool scene;
  const auto intro = scene.Allocate();
  const auto loop = scene.Allocate();
  loop->store(true); // dropped loop preparation
  Require(!intro->load(),
          "Destroying BGM loop input interrupted the active intro decoder");
  scene.CancelAll();
  Require(intro->load() && loop->load(),
          "Scene teardown failed to cancel both decoder inputs");
}

void TestExpiredWeakTokenAndBoundedRetention() {
  Pool scene;
  for (int n = 0; n != 256; ++n) {
    const auto temporary = scene.Allocate();
    Require(!temporary->load(), "A later token inherited cancellation");
  }
  // Expired weak references must not prevent cancelling the actual current
  // live jobs, even after the pool compacts its weak-token history.
  const auto persistent = scene.Allocate();
  for (int n = 0; n != 100; ++n) (void)scene.Allocate();
  scene.CancelAll();
  Require(persistent->load(), "Live token was pruned during compaction");
}
} // namespace

int main() {
  try {
    TestDiscardingOnePreparedInputDoesNotCancelOthers();
    TestSeparateBgmIntroAndLoop();
    TestExpiredWeakTokenAndBoundedRetention();
    std::cout << "SchoolDaysMediaCancellationPoolTest PASS: independent WMV/OGG, "
                 "BGM intro/loop, generation reset, expired-token compaction\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SchoolDaysMediaCancellationPoolTest FAIL: " << error.what()
              << '\n';
    return 1;
  }
}
