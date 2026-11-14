#include <Kotonoha/Filesystem/RemoteAssets.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
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
  auto remote = remoteGameAssets();
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
