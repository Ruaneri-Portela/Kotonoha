#pragma once

#include <filesystem>

namespace SchoolDays::OverlaySelection {

inline bool HasDisplayOptions(const std::filesystem::path &root) {
  std::error_code ec;
  return std::filesystem::is_regular_file(
             root / "System/Option/Option_Display.png", ec) &&
         !ec &&
         std::filesystem::is_regular_file(
             root / "System/Option/Option_Display.cmap", ec) &&
         !ec;
}

// Shared by the launcher validation and gameplay asset provider.
// An explicit --scripts/--asset-overlay/settings path always takes priority.
inline std::filesystem::path Select(const std::filesystem::path &executableRoot,
                                    const std::filesystem::path &requested = {},
                                    bool allowAutomatic = true) {
  if (!requested.empty()) return requested;
  if (!allowAutomatic) return {};
  if (executableRoot.empty()) return {};
  for (const char *folder : {"overlay", "display-options"}) {
    const auto candidate = executableRoot / folder;
    if (HasDisplayOptions(candidate)) return candidate;
  }
  return {};
}

} // namespace SchoolDays::OverlaySelection
