#pragma once

#include <functional>
#include <thread>
#include <utility>

namespace Kotonoha {

// Encapsulates native background threading versus browser-cooperative jobs.
// Games can call cooperative() and tick one job on the main loop without
// knowing the target platform or including Emscripten headers.
class BackgroundWorker {
public:
  BackgroundWorker() = default;
  BackgroundWorker(const BackgroundWorker &) = delete;
  BackgroundWorker &operator=(const BackgroundWorker &) = delete;
  ~BackgroundWorker() { join(); }

  static constexpr bool cooperative() noexcept {
#ifdef __EMSCRIPTEN__
    return true;
#else
    return false;
#endif
  }

  void start(std::function<void()> job) {
#ifndef __EMSCRIPTEN__
    thread_ = std::thread(std::move(job));
#else
    (void)job;
#endif
  }

  void join() noexcept {
#ifndef __EMSCRIPTEN__
    if (thread_.joinable())
      thread_.join();
#endif
  }

private:
#ifndef __EMSCRIPTEN__
  std::thread thread_;
#endif
};
} // namespace Kotonoha
