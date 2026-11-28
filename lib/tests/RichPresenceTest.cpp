#include <Kotonoha/integrations/RichPresence.hpp>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
using namespace Kotonoha::Integrations;

namespace {
using Clock = std::chrono::steady_clock;
struct Log {
  std::mutex mutex;
  std::condition_variable change;
  bool available = false, pauseConnect = false, throwOnPublish = false;
  bool throwOnClear = false;
  unsigned closes = 0, clears = 0;
  std::vector<Clock::time_point> connects, publishes;
  std::vector<std::string> details;
  std::thread::id worker;

  template <typename F> void wait(F test) {
    std::unique_lock<std::mutex> lock(mutex);
    const bool arrived = change.wait_for(lock, 3s, test);
    assert(arrived);
  }
};

class Fake final : public PresenceTransport {
public:
  explicit Fake(Log &log) : log_(log) {}
  bool connect() override {
    std::unique_lock<std::mutex> lock(log_.mutex);
    log_.worker = std::this_thread::get_id();
    log_.connects.push_back(Clock::now());
    log_.change.notify_all();
    log_.change.wait(lock, [&] { return !log_.pauseConnect; });
    return log_.available;
  }
  bool healthy() override {
    std::lock_guard<std::mutex> lock(log_.mutex);
    return log_.available;
  }
  bool publish(const std::string &details, const std::string &state,
               std::int64_t timestamp) override {
    (void)state;
    std::lock_guard<std::mutex> lock(log_.mutex);
    assert(timestamp == 123456 && log_.worker == std::this_thread::get_id());
    if (log_.throwOnPublish) {
      log_.throwOnPublish = false;
      throw std::runtime_error("injected");
    }
    log_.details.push_back(details);
    log_.publishes.push_back(Clock::now());
    log_.change.notify_all();
    return true;
  }
  void clear() override {
    std::lock_guard<std::mutex> lock(log_.mutex);
    ++log_.clears;
    if (log_.throwOnClear)
      throw std::runtime_error("injected");
  }
  void close() noexcept override {
    std::lock_guard<std::mutex> lock(log_.mutex);
    ++log_.closes;
    log_.change.notify_all();
  }
private:
  Log &log_;
};

void testReconnect() {
  Log log;
  RichPresence::Timing timing{100ms, 15ms, 30ms, 5ms};
  {
    RichPresence session(std::make_unique<Fake>(log), 123456, timing);
    session.setDetails("public");
    log.wait([&] { return log.connects.size() >= 4; });
    {
      std::lock_guard<std::mutex> lock(log.mutex);
      assert(log.details.empty());
      assert(log.connects[1] - log.connects[0] >= 15ms);
      assert(log.connects[2] - log.connects[1] >= 30ms);
      assert(log.connects[3] - log.connects[2] >= 30ms);
      log.pauseConnect = true;
      log.available = true;
    }
    log.wait([&] { return log.connects.size() >= 5; });
    session.setDetails("private fallback");
    {
      std::lock_guard<std::mutex> lock(log.mutex);
      log.pauseConnect = false;
      log.throwOnPublish = true;
    }
    log.change.notify_all();
    log.wait([&] { return log.details.size() == 1; });
    session.setDetails("queued public");
    session.setDetails("private fallback");
    // Once the first public update is superseded, a failed connection
    // and later reconnect must replay only the most recent value.
    {
      std::lock_guard<std::mutex> lock(log.mutex);
      log.available = false;
    }
    log.wait([&] { return log.closes >= 6; });
    {
      std::lock_guard<std::mutex> lock(log.mutex);
      log.available = true;
    }
    log.wait([&] { return log.details.size() == 2; });
    session.setDetails("next");
    log.wait([&] { return log.details.size() == 3; });
    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i)
      writers.emplace_back([&] {
        for (int j = 0; j < 500; ++j)
          session.setDetails("next");
      });
    for (auto &writer : writers)
      writer.join();
    std::lock_guard<std::mutex> lock(log.mutex);
    assert((log.details == std::vector<std::string>{
                              "private fallback", "private fallback", "next"}));
    assert(log.publishes[2] - log.publishes[1] >= timing.interval);
    assert(log.worker != std::this_thread::get_id());
    log.throwOnClear = true;
  }
  assert(log.clears == 1 && log.closes >= 7);
}

void testShutdown() {
  Log log;
  {
    RichPresence session(std::make_unique<Fake>(log), 123456,
                         {200ms, 500ms, 500ms, 20ms});
    session.setDetails("pending");
    log.wait([&] { return log.closes != 0; });
  }
  assert(log.details.empty() && log.clears == 0);
  RichPresence nullSession(nullptr, 123456, {});
  nullSession.setDetails("ignored");
}
} // namespace

int main() {
  testReconnect();
  testShutdown();
}
