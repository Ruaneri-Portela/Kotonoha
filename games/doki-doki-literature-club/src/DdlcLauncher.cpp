#include "DdlcLauncher.hpp"
#include <Ddlc/DdlcAssets.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace Ddlc {

GameLauncher::GameLauncher(std::string settingsPath, std::string directory)
    : Kotonoha::GameLauncher("Doki Doki Literature Club! - Launcher",
                             std::move(settingsPath),
                             {"Game folder (containing scripts.rpa)",
                              PathKind::Folder,
                              {},
                              "dir.txt",
                              "Use your own game files. They are read from "
                              "this folder and never modified."}) {
  setAudioChannels({"DDLC music", "DDLC sound", "DDLC voice", "DDLC ui"});
  loadPath(std::move(directory));
}

bool GameLauncher::checkPath(const std::string &path, std::string &info) {
  return validatePath(path, info);
}

bool GameLauncher::validatePath(const std::string &path, std::string &info) {
  std::error_code error;
  if (std::filesystem::is_regular_file(path, error)) {
    try {
      const auto mounted = Kotonoha::Filesystem::Filesystem::mount(path);
      bool hasRenpyScripts = mounted->exists("game/script-a1-monday.rpyc") ||
                             mounted->exists("script-a1-monday.rpyc") ||
                             mounted->exists("game/scripts.rpa") ||
                             mounted->exists("scripts.rpa");
      if (!hasRenpyScripts) {
        info = "Archive missing Ren'Py scripts";
        return false;
      }
      info = "Virtual Ren'Py archive: scripts found";
      return true;
    } catch (const std::exception &failure) {
      info = failure.what();
      return false;
    }
  }
  const auto game = std::filesystem::path(path) / "game";
  if (std::filesystem::is_regular_file(game / "scripts.rpa", error) ||
      std::filesystem::is_regular_file(game / "data.rpa", error) ||
      std::filesystem::is_regular_file(game / "script-a1-monday.rpyc", error))
    return validatePath(game.string(), info);
  if (!std::filesystem::is_directory(path, error)) {
    info = "Game folder not found: " + path;
    return false;
  }
  std::set<std::string> files;
  for (const auto &entry : std::filesystem::directory_iterator(path, error)) {
    if (!entry.is_regular_file(error))
      continue;
    auto name = entry.path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    files.insert(std::move(name));
  }
  if (error) {
    info = "Cannot read game folder: " + error.message();
    return false;
  }
  const bool hasScripts = files.count("scripts.rpa") ||
                          files.count("data.rpa") ||
                          files.count("script-a1-monday.rpyc");
  if (!hasScripts) {
    info = "Select a Ren'Py game folder containing scripts or "
           "script-a1-monday.rpyc";
    return false;
  }
  info =
      !files.count("audio.rpa") || !files.count("fonts.rpa")
          ? "Game scripts and images found; audio.rpa or fonts.rpa is missing"
          : "Game scripts, images, audio and fonts found";
  const bool characters = hasOriginalCharacters(path, error);
  if (error) {
    info = "Cannot inspect character files: " + error.message();
    return false;
  }
  if (!characters)
    info +=
        "; no sibling characters/ folder, the .chr files come from scripts.rpa";
  try {
    info += "; live character files: " +
            charactersDirectory(userDirectory()).string();
  } catch (const std::exception &failure) {
    info = std::string("Cannot open the DDLC documents directory: ") +
           failure.what();
    return false;
  }
  return true;
}

} // namespace Ddlc
