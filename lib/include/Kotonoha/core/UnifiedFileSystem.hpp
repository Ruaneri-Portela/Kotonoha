#pragma once

#include <Kotonoha/core/FileSystem.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Kotonoha {

class AssetProvider;

// Logical resource spaces. Only Saves is writable. Game resources may come
// from a native archive/ISO/overlay provider rather than physical files.
enum class FileDomain { Engine, Game, Reference, Saves };

// A small, testable facade over DirectoryLayout and AssetProvider.
// This does not change legacy FileResolver's public behavior: ports can adopt
// this service incrementally without changing existing save formats or IDs.
class UnifiedFileSystem final {
public:
  explicit UnifiedFileSystem(
      const StorageLocations &locations, std::string game = {},
      std::shared_ptr<AssetProvider> gameProvider = {});

  // Convenience factory using the engine's existing platform bootstrap.
  static UnifiedFileSystem forApplication(
      const std::string &game = {},
      std::shared_ptr<AssetProvider> gameProvider = {});

  const DirectoryLayout &layout() const noexcept { return layout_; }
  void setGameProvider(std::shared_ptr<AssetProvider> gameProvider);

  // A native path is available only for filesystem-backed data.
  // Archive/in-memory Game resources can be read() without having a path.
  std::optional<std::filesystem::path>
  find(FileDomain domain, const std::filesystem::path &relative) const;

  bool contains(FileDomain domain, const std::filesystem::path &relative) const;

  // Returns nullopt if absent, an empty vector for an existing empty asset.
  // Reads with an AssetProvider retain its overlay/original-asset precedence.
  std::optional<std::vector<std::uint8_t>>
  read(FileDomain domain, const std::filesystem::path &relative) const;

  // Atomic save replacement under the platform's writable per-app save root.
  // Rejects absolute/traversal paths and symlinks escaping the save root.
  void writeSave(const std::filesystem::path &relative,
                 const std::vector<std::uint8_t> &bytes) const;

private:
  DirectoryLayout layout_;
  std::shared_ptr<AssetProvider> gameProvider_;

  static std::filesystem::path checkedRelative(
      const std::filesystem::path &relative);
  static bool within(const std::filesystem::path &path,
                     const std::filesystem::path &root);
  static std::optional<std::filesystem::path>
  findInRoots(const std::vector<std::filesystem::path> &roots,
              const std::filesystem::path &relative);
};

} // namespace Kotonoha
