#pragma once

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace SchoolDays {

// Native byte access, including before SDL application initialization.
// Missing/disabled resources return empty; corrupt resources throw.
std::vector<unsigned char> ReadEmbeddedAsset(std::string_view logical);

// Owns the process-private compatibility directory for path-based APIs
// (SDL_ttf, libass, shader selectors and splash/media readers). The packaged
// bytes are compiled into the native binary; no game installation is written.
class EmbeddedAssets {
  std::filesystem::path session_;
  std::unique_ptr<Kotonoha::ScopedEngineAssetRoot> registration_;

public:
  EmbeddedAssets();
  ~EmbeddedAssets();
  EmbeddedAssets(const EmbeddedAssets &) = delete;
  EmbeddedAssets &operator=(const EmbeddedAssets &) = delete;
};

} // namespace SchoolDays
