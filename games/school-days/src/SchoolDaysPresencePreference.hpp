#pragma once

#include <filesystem>

namespace SchoolDays {
// Same profile as the launcher and direct startup. A missing or malformed
// sidecar always means OFF, including upgrades from before this feature.
bool readPresencePreference(const std::filesystem::path &settingsPath);
void writePresencePreference(const std::filesystem::path &settingsPath,
                             bool enabled);
} // namespace SchoolDays
