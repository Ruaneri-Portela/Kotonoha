#include "ZeroLauncher.hpp"

#include <Kotonoha/core/DiscImage.hpp>

namespace ZeroNoTsukaima {

namespace {
// SLPS-25830, the retail executable on the disc.
constexpr const char *Executable = "SLPS_258.30";
} // namespace

ZeroLauncher::ZeroLauncher(std::string settingsPath, std::string disc)
    : GameLauncher("Zero no Tsukaima - Launcher", std::move(settingsPath),
                   {"disc image (.iso or .bin)",
                    PathKind::File,
                    {{"PS2 disc image", "iso;bin;img"}, {"All files", "*"}},
                    "disc.txt",
                    "The disc is read straight from your image and never copied. Set video and upscaling in "
                    "the other tabs; everything is saved when you click Play."}) {
  loadPath(std::move(disc));
}

bool ZeroLauncher::checkPath(const std::string &path, std::string &info) {
  Kotonoha::DiscImage image;
  std::string error;
  if (!image.tryOpen(path, &error)) {
    info = "Cannot open: " + error;
    return false;
  }
  if (!image.find(Executable)) {
    info = "Disc '" + image.volumeLabel() + "' is not Zero no Tsukaima (no " + Executable + ")";
    return false;
  }
  info = "Zero no Tsukaima (" + std::string(Executable) + "), volume '" + image.volumeLabel() + "', " +
         std::to_string(image.files().size()) + " files, sector " + std::to_string(image.sectorSize());
  return true;
}

} // namespace ZeroNoTsukaima
