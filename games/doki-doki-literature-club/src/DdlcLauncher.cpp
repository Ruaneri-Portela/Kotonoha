#include "DdlcLauncher.hpp"
#include "DdlcAssets.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace Ddlc {

GameLauncher::GameLauncher(std::string settingsPath, std::string directory)
    : Kotonoha::GameLauncher("Doki Doki Literature Club! - Launcher", std::move(settingsPath),
          {"Game folder (containing scripts.rpa)", PathKind::Folder, {}, "dir.txt",
           "Use your own game files. They are read from this folder and never modified."}) {
    loadPath(std::move(directory));
}

bool GameLauncher::checkPath(const std::string& path, std::string& info) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        info = "Game folder not found: " + path;
        return false;
    }
    std::set<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (!entry.is_regular_file(error)) continue;
        auto name = entry.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return char(std::tolower(ch)); });
        files.insert(std::move(name));
    }
    if (error) {
        info = "Cannot read game folder: " + error.message();
        return false;
    }
    if (!files.count("scripts.rpa") || !files.count("images.rpa")) {
        info = "Select the game folder containing scripts.rpa and images.rpa";
        return false;
    }
    info = !files.count("audio.rpa") || !files.count("fonts.rpa")
        ? "Game scripts and images found; audio.rpa or fonts.rpa is missing"
        : "Game scripts, images, audio and fonts found";
    const bool characters = hasOriginalCharacters(path, error);
    if (error) {
        info = "Cannot inspect character files: " + error.message();
        return false;
    }
    if (!characters) info += "; no sibling characters/ folder, the .chr files come from scripts.rpa";
    try {
        info += "; live character files: " + charactersDirectory(userDirectory()).string();
    } catch (const std::exception& failure) {
        info = std::string("Cannot open the DDLC documents directory: ") + failure.what();
        return false;
    }
    return true;
}

} // namespace Ddlc
