// Loose-file source shared by XP3-based games.
#include <Kirikiri/KirikiriAssets.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Kotonoha::Providers {

KirikiriAssets::KirikiriAssets(const std::filesystem::path &directory,
                               const std::filesystem::path &overlay)
    : root(std::filesystem::canonical(directory)), overlayDir(overlay) {
  base = std::make_shared<Kotonoha::IndexedFileSystemAssetProvider>(root);
  base->setOverlay(overlayDir);
}

} // namespace Kotonoha::Providers
