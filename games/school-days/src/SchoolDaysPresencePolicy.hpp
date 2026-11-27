#pragma once

#include <Kotonoha/parsers/Ors.h>
#include <cstdint>
#include <string_view>

namespace SchoolDays {

struct PresenceScene {
  bool verified = false;
  int episode = 0;
  bool credits = false;
};

// An unknown visual script must never be treated as a public scene.
PresenceScene classifyPresenceScene(std::string_view key,
                                    const Kotonoha_orsData &script) noexcept;

struct PresenceState {
  bool protectedState = true;
  bool menu = false;
  bool options = false;
  bool routeMap = false;
  bool choice = false;
  bool finalWindow = false;
  int endingCode = -1;
  PresenceScene scene;
};

// Only static, curated strings leave this layer. Never transmit scene keys,
// script data, resource paths or player selections.
std::string_view resolvePresence(const PresenceState &state,
                                 std::string_view locale) noexcept;

} // namespace SchoolDays
