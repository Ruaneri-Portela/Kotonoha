// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Artemis/PfsArchive.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/core/AssetProvider.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Artemis {

struct SystemConfig {
  int width = 1280;
  int height = 720;
  std::string title;
  std::string bootFile;
};

// Layered AssetProvider for Artemis Engine titles.
// Mounts loose files (highest priority) over ordered .pfs + .pfs.000..999
// archive overlays using Kotonoha Filesystem V2 (zero std::ifstream).
class ArtemisAssets final : public Kotonoha::AssetProvider {
public:
  explicit ArtemisAssets(std::string_view rootDir = "",
                         std::filesystem::path overlayDir = {});

  std::string name() const override { return "Artemis assets"; }

  void setRootDirectory(std::string_view rootDir);
  const std::filesystem::path &rootDirectory() const { return rootDirectory_; }

  // Mounts an already-opened PFS archive on top of existing archives.
  void mountArchive(std::shared_ptr<PfsArchive> archive);

  const SystemConfig &systemConfig() const { return systemConfig_; }
  std::size_t archiveCount() const { return archives_.size(); }
  std::size_t entryCount() const { return mergedIndex_.size(); }

  // Discovers all .ast scripts in the merged archive/VFS.
  std::vector<std::string> discoverScripts() const;

  // Finds the best starting script (script/gamestart.ast, script/startup.ast,
  // or first non-pack scenario .ast).
  std::string findStartScript() const;

  // Resolves an Artemis logical path (supporting `:bg/`, `:fg/`, `:vo/`, etc.
  // and automatic extension probing). Returns canonical path or empty string.
  std::string resolveImagePath(std::string_view path,
                               std::string_view file) const;
  std::string resolveAudioPath(std::string_view category,
                               std::string_view file) const;
  // Returns {introPath, loopPath} for BGM tracks (supports bgmXX_a/bgmXX_b).
  std::pair<std::string, std::string>
  resolveBgmPaths(std::string_view file) const;
  std::string resolveMoviePath(std::string_view file) const;
  std::string resolveScriptPath(std::string_view file) const;

  bool containsAsset(const std::string &path) const override;
  std::vector<Uint8> readAsset(const std::string &path) const override;
  std::vector<std::string> listAsset(const std::string &prefix) const override;
  std::string localPathAsset(const std::string &path) const override;
  std::unique_ptr<Kotonoha::AssetStream>
  openStream(const std::string &path) const override;

  std::string resolveActualPath(std::string_view requestedPath) const;

private:
  struct EntryRef {
    std::string canonicalPath;
    std::shared_ptr<PfsArchive> archive; // nullptr means loose VFS file
    PfsEntry pfsEntry;
  };

  void indexLooseDirectory(const std::string &dirPath);
  void parseSystemIni();
  static std::string expandArtemisPrefix(std::string_view path);

  std::filesystem::path rootDirectory_;
  std::shared_ptr<Kotonoha::Filesystem::Filesystem> vfs_;
  std::vector<std::shared_ptr<PfsArchive>> archives_;
  std::unordered_map<std::string, EntryRef> mergedIndex_; // lower -> EntryRef
  SystemConfig systemConfig_;
};

} // namespace Artemis
