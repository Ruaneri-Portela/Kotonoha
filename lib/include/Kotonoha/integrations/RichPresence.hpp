#pragma once

#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace Kotonoha::Integrations {

// Global opt-out shared by the system settings and the debug menu. Sessions
// observe it on their worker and disconnect/reconnect without a restart.
inline std::atomic<bool> &richPresenceEnabled() noexcept {
  static std::atomic<bool> enabled{true};
  return enabled;
}
inline void setRichPresenceEnabled(bool enabled) noexcept {
  richPresenceEnabled().store(enabled, std::memory_order_release);
}

// Persisted opt-in shared by native launchers, detached game hosts and game
// processes. The value is stored in the launcher's Documents directory so every
// process sees the same switch. Games still own their per-profile preference.
inline std::filesystem::path richPresencePreferencePath() {
  return Kotonoha::documentsDirectory("me.hirameki.kotonoha") /
         "discord-presence.txt";
}

inline bool richPresencePreferenceEnabled() noexcept {
  try {
    std::ifstream file(richPresencePreferencePath(), std::ios::binary);
    char bytes[4]{};
    file.read(bytes, sizeof(bytes));
    return file.eof() && file.gcount() == 3 && bytes[0] == 'o' &&
           bytes[1] == 'n' && bytes[2] == '\n';
  } catch (...) {
    return false;
  }
}

inline void setRichPresencePreference(bool enabled) {
  Kotonoha::writeTextFileAtomically(richPresencePreferencePath().string(),
                                    enabled ? "on\n" : "off\n");
}

// Game-provided, sanitized identity for the Engine-owned presence session.
// Games only supply the data; session lifetime and IPC live in KotonohaEngine.
struct PresenceDescriptor {
  std::string applicationId;
  std::string largeImage;
  std::string name;
};

// A transport runs exclusively on its session's worker. All calls must have
// bounded latency. Only player-facing, sanitized strings belong here.
class PresenceTransport {
public:
  virtual ~PresenceTransport() = default;
  virtual bool connect() = 0;
  virtual bool healthy() = 0;
  virtual bool publish(const std::string &details, const std::string &state,
                       std::int64_t start) = 0;
  virtual void clear() = 0;
  virtual void close() noexcept = 0;
};

bool desktopPresenceAvailable() noexcept;
std::unique_ptr<PresenceTransport> discordDesktop(std::string applicationId,
                                                  std::string largeImage);

#ifdef KOTONOHA_ANDROID_DISCORD_RPC
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

  // Curated, player-facing activity states. Games may omit setDetails()
  // and set the state only; the game name is always published as details.
  enum class State { Menu, Loading, Gameplay };

  RichPresence(std::unique_ptr<PresenceTransport>, std::int64_t start, Timing,
               std::string gameName = {});
  ~RichPresence();
  void setDetails(std::string details);
  void setState(State state) noexcept;

private:
  void run() noexcept;
  std::unique_ptr<PresenceTransport> transport_;
  const std::int64_t start_;
  const Timing timing_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::string gameName_;
  std::string desired_;
  State state_ = State::Gameplay;
  bool stopping_ = false;
  std::thread worker_;
};

// Engine-owned wrapper: games only supply identity/details, the library owns
// the IPC session lifetime and the shared opt-in.
class EnginePresence final {
public:
  EnginePresence() = default;
  EnginePresence(const EnginePresence &) = delete;
  EnginePresence &operator=(const EnginePresence &) = delete;
  ~EnginePresence() { session_.reset(); }

  // Called once per Engine iteration. Lazily opens/closes the session from the
  // shared preference and publishes only curated game-provided text.
  void update(const PresenceDescriptor &descriptor,
              std::string_view details) {
    if (!richPresencePreferenceEnabled() || !richPresenceEnabled() ||
        descriptor.applicationId.empty()) {
      session_.reset();
      return;
    }
    if (!session_ || applicationId_ != descriptor.applicationId) {
      auto transport = discordPlatform(descriptor.applicationId,
                                       descriptor.largeImage,
                                       descriptor.name);
      if (!transport || !platformPresenceAvailable()) {
        session_.reset();
        return;
      }
      session_ = std::make_unique<RichPresence>(
          std::move(transport), std::time(nullptr), RichPresence::Timing{});
      applicationId_ = descriptor.applicationId;
      sentDetails_.clear();
    }
    if (!details.empty() && details != sentDetails_) {
      session_->setDetails(std::string(details));
      sentDetails_ = std::string(details);
    }
    session_->setState(RichPresence::State::Gameplay);
  }

private:
  std::unique_ptr<RichPresence> session_;
  std::string applicationId_;
  std::string sentDetails_;
};

} // namespace Kotonoha::Integrations
