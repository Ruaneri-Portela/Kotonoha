#pragma once

#include <Kotonoha/integrations/RichPresence.hpp>
#include <filesystem>
#include <string_view>

namespace SchoolDays {
class Runtime;

class DiscordPresence {
public:
  void start(const std::filesystem::path &settingsPath) noexcept;
  void update(Runtime &runtime) noexcept;

private:
  std::unique_ptr<Kotonoha::Integrations::RichPresence> session_;
  std::string_view last_;
};
} // namespace SchoolDays
