#include "SchoolDaysPresencePreference.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
  namespace fs = std::filesystem;
  using namespace SchoolDays;
  const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = fs::temp_directory_path() / ("sd-presence-" + std::to_string(serial));
  assert(fs::create_directory(dir));
  const auto settings = dir / "settings.json";
  const auto unrelated = dir / "Kotonoha.ini";
  std::ofstream(unrelated) << "EnableLxh=1\n";
  assert(!readPresencePreference(settings));
  writePresencePreference(settings, true);
  assert(readPresencePreference(settings));
  writePresencePreference(settings, false);
  assert(!readPresencePreference(settings));
  std::ofstream(dir / "discord-presence.txt") << "on\ntrailing";
  assert(!readPresencePreference(settings));
  assert(!fs::exists(settings));
  std::string line;
  assert(std::getline(std::ifstream(unrelated), line) && line == "EnableLxh=1");
  fs::remove_all(dir);
}
