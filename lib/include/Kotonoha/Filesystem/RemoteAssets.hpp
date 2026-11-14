#pragma once
#include <Kotonoha/Filesystem/IMount.hpp>
#include <filesystem>
#include <memory>
namespace Kotonoha::Filesystem {
class FetchAssetProvider;
class Filesystem;

// Set once during the browser's asynchronous manifest bootstrap, before
// SDL_AppInit. Apps can also explicitly pass a different IMount to
// Filesystem::mount(). The registry never includes saves or documents.
void setRemoteGameAssets(std::shared_ptr<FetchAssetProvider> provider);
std::shared_ptr<FetchAssetProvider> remoteGameAssets();

// Local directory is preferred if valid; remote manifest entries fill gaps
// or replace files with mismatching size. The local directory may not exist.
std::shared_ptr<Filesystem> mountGameAssets(
    const std::filesystem::path &localRoot);
} // namespace Kotonoha::Filesystem
