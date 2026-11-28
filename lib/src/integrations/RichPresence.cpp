#include <Kotonoha/integrations/RichPresence.hpp>

#include <algorithm>
#include <utility>

namespace Kotonoha::Integrations {
namespace {
const char *stateLabel(RichPresence::State state) noexcept {
  using State = RichPresence::State;
  switch (state) {
  case State::Menu: return "Main menu";
  case State::Loading: return "Loading";
  case State::Gameplay: return "In game";
  }
  return "In game";
}
} // namespace

RichPresence::RichPresence(std::unique_ptr<PresenceTransport> transport,
                           std::int64_t start, Timing timing,
                           std::string gameName)
    : transport_(std::move(transport)), start_(start), timing_(timing),
      gameName_(std::move(gameName)), desired_(gameName_) {
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
    if (details.empty())
      return;
    // The curated detail is appended to the game name, never replaces it.
    const auto next = gameName_.empty()
                          ? details
                          : gameName_ + " — " + details;
    if (next == desired_)
      return;
    desired_ = next;
  }
  changed_.notify_one();
}

void RichPresence::setState(State state) noexcept {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
  }
  changed_.notify_one();
}

void RichPresence::run() noexcept {
  using Clock = std::chrono::steady_clock;
  std::string sent;
  State sentState = State::Gameplay;
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
    if (!richPresenceEnabled().load(std::memory_order_acquire)) {
      if (connected) {
        lock.unlock();
        transport_->close();
        lock.lock();
        connected = false;
        sent.clear();
        sentState = State::Gameplay;
      }
      reconnect = Clock::now() + timing_.retry;
      changed_.wait_until(lock, reconnect);
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
    } else if (connected && (desired_ != sent || state_ != sentState) &&
               now >= publishAfter) {
      // Take the newest value AFTER connection and backoff. No FIFO backlog.
      auto latest = desired_;
      const auto currentState = state_;
      const auto label = stateLabel(currentState);
      lock.unlock();
      try { ok = transport_->publish(latest, label, start_); } catch (...) { ok = false; }
      lock.lock();
      publishAfter = Clock::now() + timing_.interval;
      if (ok) {
        sent = std::move(latest);
        sentState = currentState;
        delay = timing_.retry;
      }
    } else {
      auto wake = connected ? checkAfter : reconnect;
      if (connected && (desired_ != sent || state_ != sentState))
        wake = std::min(wake, publishAfter);
      changed_.wait_until(lock, wake);
    }

    if (!ok) {
      lock.unlock();
      transport_->close();
      lock.lock();
      connected = false;
      sent.clear();
      sentState = State::Gameplay;
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
