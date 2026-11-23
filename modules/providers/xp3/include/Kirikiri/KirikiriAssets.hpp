#pragma once
// Public API of the reusable Kotonoha::ProviderXP3 module.

#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::Providers {

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
  explicit KirikiriAssets(
      std::shared_ptr<Kotonoha::Filesystem::Filesystem> source,
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

  // Read-only summary of the mounted XP3 indexes. Entries whose `info` flags
  // have bit 31 set carry encrypted payloads; the parser keeps them
  // discoverable but their bytes are not decodable without the game's
  // native protection filter.
  struct Xp3IndexReport {
    std::size_t archives = 0;
    std::size_t entries = 0;
    std::size_t protectedEntries = 0;
  };
  Xp3IndexReport xp3IndexReport() const;

  // Mounts an XP3 archive by path (relative to the game root or absolute).
  // Only the file table is parsed; entry data is read on demand.
  void mountXp3(const std::filesystem::path &path);
  void mountAllXp3();

private:
  std::string decodeTextAsset(const std::string &id,
                              const std::vector<Uint8> &bytes,
                              const char *encoding) const override;
  struct Xp3Segment {
    std::uint64_t offset = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t originalSize = 0;
    bool compressed = false;
  };
  struct Xp3Entry {
    std::string name;
    std::string archiveId; // virtual identifier for the XP3 source
    std::uint64_t originalSize = 0;
    bool protectedEntry = false; // `info` flags bit 31 (encrypted payload)
    // A Kirikiri storage may span many raw or zlib-compressed segments.
    // Keep the physical spans rather than incorrectly returning only one.
    std::vector<Xp3Segment> segments;
  };

  std::filesystem::path root;
  std::shared_ptr<Kotonoha::Filesystem::Filesystem> source_;
  std::filesystem::path overlayDir;
  std::shared_ptr<Kotonoha::AssetProvider> base;
  // XP3 archives mounted by the game, keyed by archive file name.
  // Mount order is significant: later archives override earlier ones. This
  // is how the usual data.xp3 -> patch.xp3 stack behaves in Kirikiri.
  std::vector<std::pair<std::string, std::vector<Xp3Entry>>> archives;

  std::string resolveId(const std::string &id) const;
  // Match logical XP3 paths independent of ASCII case and slash direction.
  // A folded collision is rejected instead of selecting a random asset.
  const Xp3Entry *findXp3Entry(const std::string &id,
                              bool *collision = nullptr) const;
  bool readXp3(const std::string &id);
  std::vector<std::uint8_t> extractXp3Entry(const Xp3Entry &entry) const;
};

} // namespace Kotonoha::Providers
