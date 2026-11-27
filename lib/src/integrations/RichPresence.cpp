#include <Kotonoha/integrations/RichPresence.hpp>

#include <algorithm>
#include <utility>

namespace Kotonoha::Integrations {

RichPresence::RichPresence(std::unique_ptr<PresenceTransport> transport,
                           std::int64_t start, Timing timing)
    : transport_(std::move(transport)), start_(start), timing_(timing) {
  if (transport_)
    worker_ = std::thread(&RichPresence::run, this);
}

RichPresence::~RichPresence() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  changed_.notify_one();
  if (worker_.joinable())
    worker_.join();
}

void RichPresence::setDetails(std::string details) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (details == desired_ || details.empty())
      return;
    desired_ = std::move(details);
  }
  changed_.notify_one();
}

void RichPresence::run() noexcept {
  using Clock = std::chrono::steady_clock;
  std::string sent;
  bool connected = false;
  auto delay = timing_.retry;
  auto reconnect = Clock::now();
  auto publishAfter = reconnect;
  auto checkAfter = reconnect;
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stopping_) {
    auto now = Clock::now();
    if (desired_.empty()) {
      changed_.wait(lock);
      continue;
    }
    bool ok = true;
    if (!connected && now >= reconnect) {
      lock.unlock();
      try { ok = transport_->connect(); } catch (...) { ok = false; }
      lock.lock();
      connected = ok;
      checkAfter = Clock::now() + timing_.heartbeat;
    } else if (connected && now >= checkAfter) {
      lock.unlock();
      try { ok = transport_->healthy(); } catch (...) { ok = false; }
      lock.lock();
      checkAfter = Clock::now() + timing_.heartbeat;
    } else if (connected && desired_ != sent && now >= publishAfter) {
      // Take the newest value AFTER connection and backoff. No FIFO backlog.
      auto latest = desired_;
      lock.unlock();
      try { ok = transport_->publish(latest, start_); } catch (...) { ok = false; }
      lock.lock();
      publishAfter = Clock::now() + timing_.interval;
      if (ok) {
        sent = std::move(latest);
        delay = timing_.retry;
      }
    } else {
      auto wake = connected ? checkAfter : reconnect;
      if (connected && desired_ != sent)
        wake = std::min(wake, publishAfter);
      changed_.wait_until(lock, wake);
    }

    if (!ok) {
      lock.unlock();
      transport_->close();
      lock.lock();
      connected = false;
      sent.clear();
      reconnect = Clock::now() + delay;
      delay = std::min(timing_.maxRetry, delay * 2);
    }
  }
  lock.unlock();
  if (connected) {
    try { transport_->clear(); } catch (...) {}
  }
  transport_->close();
}

} // namespace Kotonoha::Integrations
