#include <Kotonoha/integrations/RichPresence.hpp>

#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>
#include <android/log.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {
std::atomic<bool> sdkReady{false};
}

namespace {
// SDK callbacks are pumped on this transport's single worker. No account
// login, voice service, activity join secrets or personal tokens are used.
class AndroidDiscord final : public PresenceTransport {
public:
  AndroidDiscord(std::uint64_t id, std::string image, std::string name)
      : id_(id), image_(std::move(image)), name_(std::move(name)) {}
  bool connect() override {
    if (!sdkReady.load()) return false;
    client_ = std::make_unique<discordpp::Client>();
    client_->SetApplicationId(id_);
    if (client_->GetApplicationId() != id_) {
      __android_log_write(ANDROID_LOG_WARN, "KotonohaPresence",
                          "Discord SDK application ID mismatch");
      return false;
    }
    __android_log_write(ANDROID_LOG_INFO, "KotonohaPresence",
                        "Discord SDK application ID verified");
    return true;
  }
  bool healthy() override {
    if (!sdkReady.load() || !client_) return false;
    discordpp::RunCallbacks();
    return true;
  }
  bool publish(const std::string &details, const std::string &state,
               std::int64_t start) override {
    if (!client_ || !sdkReady.load()) return false;
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Playing);
    // The SDK may replace Name with the registered application name.
    // Supply the game's display name for mobile transports that honor it,
    // while keeping application identity controlled by SetApplicationId.
    if (!name_.empty()) activity.SetName(name_);
    activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Name);
    activity.SetDetails(details);
    activity.SetState(state);
    discordpp::ActivityTimestamps timestamps;
    timestamps.SetStart(static_cast<std::uint64_t>(start) * 1000u);
    activity.SetTimestamps(timestamps);
    if (!image_.empty()) {
      discordpp::ActivityAssets assets;
      assets.SetLargeImage(image_);
      activity.SetAssets(assets);
    }
    auto result = std::make_shared<std::atomic<int>>(0);
    client_->UpdateRichPresence(std::move(activity),
        [result](discordpp::ClientResult value) {
          __android_log_print(value.Successful() ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                              "KotonohaPresence", "Discord presence result: success=%d type=%d code=%d",
                              value.Successful() ? 1 : 0,
                              static_cast<int>(value.Type()), value.ErrorCode());
          result->store(value.Successful() ? 1 : -1);
        });
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < deadline && sdkReady.load()) {
      discordpp::RunCallbacks();
      if (result->load() != 0) return result->load() == 1;
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
  }
  void clear() override {
    if (client_) {
      client_->ClearRichPresence();
      discordpp::RunCallbacks();
    }
  }
  void close() noexcept override {
    try {
      clear();
      client_.reset();
      discordpp::RunCallbacks();
    } catch (...) {
      client_.reset();
    }
  }
private:
  std::uint64_t id_;
  std::string image_;
  std::string name_;
  std::unique_ptr<discordpp::Client> client_;
};
}

bool androidPresenceAvailable() noexcept { return sdkReady.load(); }

std::unique_ptr<PresenceTransport> discordAndroid(std::string id,
                                                  std::string image,
                                                  std::string name) {
  return std::make_unique<AndroidDiscord>(std::stoull(id), std::move(image),
                                          std::move(name));
}
} // namespace Kotonoha::Integrations
