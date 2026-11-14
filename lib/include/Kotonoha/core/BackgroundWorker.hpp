#pragma once

#include <functional>
#include <memory>

namespace Kotonoha {

// Executes jobs in a native worker thread when supported. In the
// browser's single-thread build, callers may run one job cooperatively
// through their regular update/drain loop instead.
class BackgroundWorker {
public:
  explicit BackgroundWorker(std::function<void()> job);
  ~BackgroundWorker();

  BackgroundWorker(const BackgroundWorker &) = delete;
  BackgroundWorker &operator=(const BackgroundWorker &) = delete;

  bool cooperative() const noexcept;
  void join();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Kotonoha
