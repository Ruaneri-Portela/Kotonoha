#include "SnowLauncher.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace SnowRadish {

namespace {
constexpr const char *Packs[] = {"SCRIPT.DAT", "GRAPHIC.DAT", "SOUND.DAT", "VOICE.DAT"};

bool HasFile(const std::filesystem::path &directory, const std::string &name) {
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
    std::string file = entry.path().filename().string();
    std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (file == name && entry.is_regular_file(ec)) return true;
  }
  return false;
}
} // namespace

SnowLauncher::SnowLauncher(std::string settingsPath, std::string directory)
    : GameLauncher("Snow Radish Vacation!! - Launcher", std::move(settingsPath),
                   {"game folder (with SCRIPT.DAT)", PathKind::Folder, {}, "dir.txt",
                    "The DAT files are read straight from your folder and never copied. Set video and "
                    "upscaling in the other tabs; everything is saved when you click Play."}) {
  loadPath(std::move(directory));
}

bool SnowLauncher::checkPath(const std::string &path, std::string &info) {
  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec)) {
    info = "Folder not found: " + path;
    return false;
  }
  std::string missing;
  for (const char *pack : Packs)
    if (!HasFile(path, pack)) missing += std::string(missing.empty() ? "" : ", ") + pack;
  // SCRIPT and GRAPHIC are required; without SOUND / VOICE the game is silent.
  if (!HasFile(path, "SCRIPT.DAT") || !HasFile(path, "GRAPHIC.DAT")) {
    info = "Folder without the game packs (missing " + missing + ")";
    return false;
  }
  info = missing.empty() ? "Snow Radish Vacation!!: SCRIPT, GRAPHIC, SOUND and VOICE.DAT found"
                         : "Snow Radish Vacation!! (missing " + missing + ": no such audio)";
  return true;
}

} // namespace SnowRadish
