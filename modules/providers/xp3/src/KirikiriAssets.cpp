// Loose-file source shared by XP3-based games.
#include <Kirikiri/KirikiriAssets.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace Kotonoha::Providers {

KirikiriAssets::KirikiriAssets(const std::filesystem::path &directory,
                               const std::filesystem::path &overlay)
    : KirikiriAssets(
          Kotonoha::Filesystem::Filesystem::mount(directory.string()),
          overlay) {
  root = directory;
}

KirikiriAssets::KirikiriAssets(
    std::shared_ptr<Kotonoha::Filesystem::Filesystem> source,
    const std::filesystem::path &overlay)
    : overlayDir(overlay), source_(std::move(source)) {
  if (!source_)
    throw std::invalid_argument("Kirikiri requires a virtual source");
  if (!overlayDir.empty())
    source_->addOverlay(overlayDir.string());
  base = std::make_shared<Kotonoha::IndexedFileSystemAssetProvider>(source_);
  if (!overlayDir.empty())
    base->setOverlay(overlayDir);
}

} // namespace Kotonoha::Providers
