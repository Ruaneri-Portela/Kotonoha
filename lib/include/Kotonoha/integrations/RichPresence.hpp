#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace Kotonoha::Integrations {

// A transport runs exclusively on its session's worker. All calls must have
// bounded latency. Only player-facing, sanitized strings belong here.
class PresenceTransport {
public:
  virtual ~PresenceTransport() = default;
  virtual bool connect() = 0;
  virtual bool healthy() = 0;
  virtual bool publish(const std::string &details, std::int64_t start) = 0;
  virtual void clear() = 0;
  virtual void close() noexcept = 0;
};

bool desktopPresenceAvailable() noexcept;
std::unique_ptr<PresenceTransport> discordDesktop(std::string applicationId,
                                                  std::string largeImage);

#ifdef KOTONOHA_ANDROID_DISCORD_RPC
// The host calls this only after initializing the SDK with its Android Activity.
// Keep the SDK context valid until all RichPresence sessions are destroyed.
void setAndroidPresenceReady(bool ready) noexcept;
bool androidPresenceAvailable() noexcept;
std::unique_ptr<PresenceTransport> discordAndroid(std::string applicationId,
                                                  std::string largeImage,
                                                  std::string applicationName);
#endif

inline bool platformPresenceAvailable() noexcept {
#ifdef KOTONOHA_ANDROID_DISCORD_RPC
  return androidPresenceAvailable();
#else
  return desktopPresenceAvailable();
#endif
}

inline std::unique_ptr<PresenceTransport> discordPlatform(
    std::string applicationId, std::string largeImage,
    std::string applicationName = {}) {
#ifdef KOTONOHA_ANDROID_DISCORD_RPC
  return discordAndroid(std::move(applicationId), std::move(largeImage),
                        std::move(applicationName));
#else
  (void)applicationName;
  return discordDesktop(std::move(applicationId), std::move(largeImage));
#endif
}

class RichPresence final {
public:
  using Duration = std::chrono::milliseconds;
  struct Timing {
    Duration interval{5000};
    Duration retry{1000};
    Duration maxRetry{60000};
    Duration heartbeat{1000};
  };

  RichPresence(std::unique_ptr<PresenceTransport>, std::int64_t start, Timing);
  ~RichPresence();
  void setDetails(std::string details);

private:
  void run() noexcept;
  std::unique_ptr<PresenceTransport> transport_;
  const std::int64_t start_;
  const Timing timing_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::string desired_;
  bool stopping_ = false;
  std::thread worker_;
};

} // namespace Kotonoha::Integrations
