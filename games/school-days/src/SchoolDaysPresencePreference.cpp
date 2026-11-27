#include "SchoolDaysPresencePreference.hpp"

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <fstream>

namespace SchoolDays {
namespace {
std::filesystem::path preference(const std::filesystem::path &settings) {
  return settings.parent_path() / "discord-presence.txt";
}
} // namespace

bool readPresencePreference(const std::filesystem::path &settingsPath) {
  if (settingsPath.empty())
    return false;
  std::ifstream file(preference(settingsPath), std::ios::binary);
  char bytes[4]{};
  file.read(bytes, sizeof(bytes));
  return file.eof() && file.gcount() == 3 && bytes[0] == 'o' &&
         bytes[1] == 'n' && bytes[2] == '\n';
}

void writePresencePreference(const std::filesystem::path &settingsPath,
                             bool enabled) {
  if (settingsPath.empty())
    return;
  Kotonoha::writeTextFileAtomically(preference(settingsPath).string(),
                                    enabled ? "on\n" : "off\n");
}
} // namespace SchoolDays
