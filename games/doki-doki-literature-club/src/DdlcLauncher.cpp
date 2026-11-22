#include "DdlcLauncher.hpp"
#include <Ddlc/DdlcAssets.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Kotonoha/Filesystem/EngineDetector.hpp>

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
  try {
    const auto mounted = Kotonoha::Filesystem::Filesystem::mount(path);
    if (!mounted ||
        Kotonoha::Filesystem::detectEngine(*mounted).kind !=
            Kotonoha::Filesystem::EngineKind::RenPy) {
      info = "Select a Ren'Py installation containing .rpa, .rpyc or "
             ".rpymc files";
      return false;
    }
    info = "Ren'Py package detected; this port requires compiled scripts";
    return true;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}

} // namespace Ddlc
