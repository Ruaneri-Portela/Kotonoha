#include "SchoolDaysDiscordPresence.hpp"
#include "SchoolDaysPresencePreference.hpp"
#include "SchoolDaysRuntime.hpp"

#include <ctime>

namespace SchoolDays {
void DiscordPresence::start(const std::filesystem::path &settingsPath) noexcept {
  using namespace Kotonoha::Integrations;
  try {
    if (!platformPresenceAvailable() || !readPresencePreference(settingsPath))
      return;
    // The application icon is already registered. A separate presence asset
    // key is not confirmed; do not send an unregistered large_image key.
    auto transport = discordPlatform("1575710571171684434", {}, "School Days HQ");
    session_ = std::make_unique<RichPresence>(std::move(transport),
                                              std::time(nullptr),
                                              RichPresence::Timing{});
  } catch (...) {
    // Presence is optional, including on hosts unable to create a worker.
  }
}

void DiscordPresence::update(Runtime &runtime) noexcept {
  if (!session_)
    return;
  try {
    // The resolver returns catalog strings with static lifetime.
    const auto details = runtime.presenceDetails();
    if (last_ != details) {
      session_->setDetails(std::string(details));
      last_ = details;
    }
  } catch (...) {
    session_.reset();
  }
}
} // namespace SchoolDays
