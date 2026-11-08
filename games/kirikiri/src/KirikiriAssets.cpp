#include <Kirikiri/KirikiriAssets.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Kirikiri {

KirikiriAssets::KirikiriAssets(const std::filesystem::path &directory,
                               const std::filesystem::path &overlay)
    : root(std::filesystem::canonical(directory)), overlayDir(overlay) {
  base = std::make_shared<Kotonoha::IndexedFileSystemAssetProvider>(root);
  base->setOverlay({});
}

} // namespace Kirikiri
