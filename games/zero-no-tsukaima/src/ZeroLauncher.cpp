#include "ZeroLauncher.hpp"

#include <Kotonoha/core/DiscImage.hpp>

namespace ZeroNoTsukaima {

namespace {
// Retail executables supported on the Zero no Tsukaima discs.
constexpr const char *Executables[] = {"SLPS_257.09", "SLPS_258.98", "SLPS_258.30"};
constexpr size_t NumExecutables = sizeof(Executables) / sizeof(Executables[0]);
} // namespace

ZeroLauncher::ZeroLauncher(std::string settingsPath, std::string disc)
    : GameLauncher(
          "Zero no Tsukaima - Launcher", std::move(settingsPath),
          {"disc image (.iso or .bin)",
           PathKind::File,
           {{"PS2 disc image", "iso;bin;img"}, {"All files", "*"}},
           "disc.txt",
           "The disc is read straight from your image and never copied. Set "
           "video and upscaling in "
           "the other tabs; everything is saved when you click Play."}) {
  setAudioChannels({"bgm", "voice", "se"});
  loadPath(std::move(disc));
}

bool ZeroLauncher::checkPath(const std::string &path, std::string &info) {
  return validatePath(path, info);
}

bool ZeroLauncher::validatePath(const std::string &path, std::string &info) {
  Kotonoha::DiscImage image;
  std::string error;
  if (!image.tryOpen(path, &error)) {
    info = "Cannot open: " + error;
    return false;
  }
  const char *found = nullptr;
  for (size_t i = 0; i < NumExecutables; ++i) {
    if (image.find(Executables[i])) {
      found = Executables[i];
      break;
    }
  }
  if (!found) {
    info = "Disc '" + image.volumeLabel() + "' is not Zero no Tsukaima (no supported executable)";
    return false;
  }
  info = "Zero no Tsukaima (" + std::string(found) + "), volume '" +
         image.volumeLabel() + "', " + std::to_string(image.files().size()) +
         " files, sector " + std::to_string(image.sectorSize());
  return true;
}

} // namespace ZeroNoTsukaima
