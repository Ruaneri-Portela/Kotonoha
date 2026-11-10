#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kirikiri {

// Asset provider for Kirikiri2 / KirikiriZ games. Reads loose files and
// XP3 archives (the engine's container format). Game data lives under a
// directory whose root contains the KAG scenario scripts and the XP3
// package(s) (commonly data.xp3).
//
// XP3 archives are NOT loaded entirely into memory: only their file table
// is parsed at mount time, and individual entries are read from disk on
// demand.
class KirikiriAssets : public Kotonoha::AssetProvider {
public:
  explicit KirikiriAssets(const std::filesystem::path &directory,
                          const std::filesystem::path &overlay = {});

  bool containsAsset(const std::string &id) const override;
  std::vector<std::uint8_t> readAsset(const std::string &id) const override;
  std::vector<std::string> listAsset(const std::string &prefix) const override;
  std::string localPathAsset(const std::string &id) const override;
  Kotonoha::SurfacePtr loadSurfaceAsset(const std::string &id) const override;

  std::string name() const override { return "KirikiriAssets"; }
  // Returns every scenario file found in the data (files ending in .ks,
  // case-insensitive), sorted by name.
  std::vector<std::string> scenarioFiles() const;

  // Chooses the most likely game entry point instead of relying on the
  // alphabetical order of scenario files.
  std::string startupScenario() const;

  // Mounts an XP3 archive by path (relative to the game root or absolute).
  // Only the file table is parsed; entry data is read on demand.
  void mountXp3(const std::filesystem::path &path);

private:
  struct Xp3Entry {
    std::string name;
    std::filesystem::path archivePath; // path to the XP3 file on disk
    std::uint64_t offset = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t originalSize = 0;
    bool compressed = false;
  };

  std::filesystem::path root;
  std::filesystem::path overlayDir;
  std::shared_ptr<Kotonoha::AssetProvider> base;
  // XP3 archives mounted by the game, keyed by archive file name.
  // Mount order is significant: later archives override earlier ones. This
  // is how the usual data.xp3 -> patch.xp3 stack behaves in Kirikiri.
  std::vector<std::pair<std::string, std::vector<Xp3Entry>>> archives;

  std::string resolveId(const std::string &id) const;
  bool readXp3(const std::filesystem::path &path);
  std::vector<std::uint8_t> extractXp3Entry(const Xp3Entry &entry) const;
};

} // namespace Kirikiri
