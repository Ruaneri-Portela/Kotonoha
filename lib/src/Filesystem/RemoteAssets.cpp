#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/Path.hpp>
#include <Kotonoha/Filesystem/Mounts/DirectoryMount.hpp>
#include <Kotonoha/Filesystem/Mounts/FallbackMount.hpp>
#include <Kotonoha/Filesystem/Sources/FetchAssetProvider.hpp>

#include <filesystem>
#include <mutex>
#include <utility>

namespace Kotonoha::Filesystem {
namespace {
std::mutex registryMutex;
std::shared_ptr<FetchAssetProvider> registered;

// For game-assets/<game>/ resources, reinterpret the remote manifest with
// paths relative to that subfolder. Consumers never need URL string slicing.
class SubtreeMount final : public IMount {
  std::shared_ptr<IMount> source_;
  std::string prefix_;
  std::string full(std::string_view relative) const {
    const auto key = normalizePath(relative, true);
    return key.empty() ? prefix_ : prefix_ + "/" + key;
  }
public:
  SubtreeMount(std::shared_ptr<IMount> source, std::string prefix)
      : source_(std::move(source)), prefix_(normalizePath(prefix)) {}
  bool exists(std::string_view path) const override {
    return source_->exists(full(path));
  }
  FileInfo stat(std::string_view path) const override {
    auto info = source_->stat(full(path));
    const auto name = normalizePath(path, true);
    info.path = name;
    return info;
  }
  std::vector<FileInfo> list(std::string_view directory) const override {
    const auto path = full(directory);
    auto entries = source_->list(path);
    for (auto &item : entries) {
      const auto child = item.path.substr(prefix_.size() + 1);
      item.path = child;
    }
    return entries;
  }
  std::unique_ptr<FileHandle> open(std::string_view path) const override {
    return source_->open(full(path));
  }
};
std::string assetSubfolder(const std::filesystem::path &root) {
  std::string sub;
  bool found = false;
  for (const auto &part : root.lexically_normal()) {
    const auto name = part.generic_string();
    if (found) {
      if (!sub.empty()) sub += "/";
      sub += name;
    } else if (name == "game-assets") {
      found = true;
    }
  }
  return found ? sub : std::string{};
}
} // namespace

void setRemoteGameAssets(std::shared_ptr<FetchAssetProvider> provider) {
  std::lock_guard<std::mutex> lock(registryMutex);
  registered = std::move(provider);
}
std::shared_ptr<FetchAssetProvider> remoteGameAssets() {
  std::lock_guard<std::mutex> lock(registryMutex);
  return registered;
}
std::shared_ptr<Filesystem> mountGameAssets(
    const std::filesystem::path &root) {
  std::shared_ptr<IMount> local;
  std::error_code ec;
  if (!root.empty() && std::filesystem::is_directory(root, ec)) {
    // Invalid or unsafe directories are not accepted as local mounts.
    try {
      local = std::make_shared<DirectoryMount>(root);
    } catch (const FilesystemError &) {
      if (!remoteGameAssets())
        throw;
    }
  }
  std::shared_ptr<IMount> remote = remoteGameAssets();
  const auto subfolder = assetSubfolder(root);
  // Some packaged manifests already use the selected game's asset folder
  // as their logical root; do not strip a prefix that is not in the index.
  if (remote && !subfolder.empty() &&
      (remote->manifestFree() || remote->hasIndexedDirectory(subfolder)))
    remote = std::make_shared<SubtreeMount>(std::move(remote), subfolder);
  if (remote)
    return Filesystem::mount(
        std::make_shared<FallbackMount>(std::move(local),
                                        std::move(remote)));
  if (!local)
    throw FilesystemError(ErrorCode::NotFound,
                          "game-assets directory is missing and HTTP(S) "
                          "manifest has not been configured");
  return Filesystem::mount(std::move(local));
}
} // namespace Kotonoha::Filesystem
