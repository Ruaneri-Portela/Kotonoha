#include <Kotonoha/core/BackgroundWorker.hpp>

#ifndef __EMSCRIPTEN__
#include <thread>
#endif

namespace Kotonoha {

struct BackgroundWorker::Impl {
#ifndef __EMSCRIPTEN__
  std::thread thread;
  explicit Impl(std::function<void()> job) : thread(std::move(job)) {}
  void join() {
    if (thread.joinable())
      thread.join();
  }
#else
  explicit Impl(std::function<void()>) {}
  void join() {}
#endif
};

BackgroundWorker::BackgroundWorker(std::function<void()> job)
    : impl_(std::make_unique<Impl>(std::move(job))) {}

BackgroundWorker::~BackgroundWorker() { join(); }

bool BackgroundWorker::cooperative() const noexcept {
#ifdef __EMSCRIPTEN__
  return true;
#else
  return false;
#endif
}

void BackgroundWorker::join() {
  if (impl_)
    impl_->join();
}

} // namespace Kotonoha
