#include "SnowLauncher.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#endif

namespace SnowRadish {

namespace {
constexpr const char *Packs[] = {"SCRIPT.DAT", "GRAPHIC.DAT", "SOUND.DAT",
                                 "VOICE.DAT"};

bool HasFile(const std::filesystem::path &directory, const std::string &name) {
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
    std::string file = entry.path().filename().string();
    std::transform(file.begin(), file.end(), file.begin(),
                   [](unsigned char c) { return char(std::toupper(c)); });
    if (file == name && entry.is_regular_file(ec))
      return true;
  }
  return false;
}

std::filesystem::path PackDirectory(const std::filesystem::path &root) {
  for (const auto &candidate : {root, root / "pack", root / "PACK",
                                root / "data" / "pack", root / "DATA" / "PACK"})
    if (HasFile(candidate, "SCRIPT.DAT") && HasFile(candidate, "GRAPHIC.DAT"))
      return candidate;
  return root;
}
} // namespace

SnowLauncher::SnowLauncher(std::string settingsPath, std::string directory)
    : GameLauncher(
          "Snow Radish Vacation!! - Launcher", std::move(settingsPath),
          {"game folder (with SCRIPT.DAT)",
           PathKind::Folder,
           {},
           "dir.txt",
           "Choose a DAT folder or ISO image. Folder data is read "
           "in place; ISO data uses a temporary internal mount. Set video and "
           "upscaling in the other tabs; everything is saved when you "
           "click Play."}) {
  setAudioChannels({"bgm", "voice", "se"});
  loadPath(std::move(directory));
}

bool SnowLauncher::checkPath(const std::string &path, std::string &info) {
  return validatePath(path, info);
}

bool SnowLauncher::validatePath(const std::string &path, std::string &info) {
#ifdef __EMSCRIPTEN__
  // In a manifest-free browser session only the named DAT files are known.
  // std::filesystem::directory_iterator cannot enumerate HTTP directories.
  if (Kotonoha::Filesystem::remoteGameAssets()) {
    try {
      auto vfs = Kotonoha::Filesystem::mountGameAssets(path);
      for (const char *root : {"", "pack/", "data/pack/"}) {
        const std::string script = std::string(root) + "SCRIPT.DAT";
        const std::string graphic = std::string(root) + "GRAPHIC.DAT";
        if (vfs->exists(script) && vfs->exists(graphic)) {
          info = "Snow Radish DAT packs available in virtual " +
                 std::string(root) + " folder";
          return true;
        }
      }
    } catch (const Kotonoha::Filesystem::FilesystemError &error) {
      info = error.what();
    }
    info = "Snow Radish VFS cannot locate SCRIPT.DAT and GRAPHIC.DAT";
    return false;
  }
#endif
  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec)) {
    info = "Folder not found: " + path;
    return false;
  }
  const auto directory = PackDirectory(path);
  std::string missing;
  for (const char *pack : Packs)
    if (!HasFile(directory, pack))
      missing += std::string(missing.empty() ? "" : ", ") + pack;
  // SCRIPT and GRAPHIC are required; without SOUND / VOICE the game is silent.
  if (!HasFile(directory, "SCRIPT.DAT") || !HasFile(directory, "GRAPHIC.DAT")) {
    info = "Folder without the game packs (missing " + missing + ")";
    return false;
  }
  info =
      missing.empty()
          ? "Snow Radish Vacation!!: SCRIPT, GRAPHIC, SOUND and VOICE.DAT found"
          : "Snow Radish Vacation!! (missing " + missing + ": no such audio)";
  return true;
}

} // namespace SnowRadish
