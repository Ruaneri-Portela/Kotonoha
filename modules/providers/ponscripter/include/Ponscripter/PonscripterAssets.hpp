// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Ponscripter/Archive.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ponscripter {

// Case-insensitive VFS + SAR/NSA archive AssetProvider for PONscripter titles
// (including SayaNoUta_PT-BR sound/<dir>/<clip>.ogg -> voice/<dir>/<clip>.ogg
// fallback).
class PonscripterAssets final : public Kotonoha::AssetProvider {
public:
  explicit PonscripterAssets(std::string_view rootDir = "",
                             std::filesystem::path overlayDir = {});

  std::string name() const override { return "PONscripter assets"; }

  void setRootDirectory(std::string_view rootDir);
  const std::filesystem::path &rootDirectory() const { return rootDirectory_; }

  void mountArchive(std::vector<std::uint8_t> data, ArchiveFormat format,
                    std::size_t nsaOffset = 0);
  void mountArchiveFromVfs(const std::string &vfsPath, ArchiveFormat format,
                           std::size_t nsaOffset = 0);

  // Concatenates 0.utf or 00.utf..99.utf (or 0.txt..99.txt) in numeric order.
  std::vector<std::pair<std::string, std::string>> discoverScripts() const;

  bool containsAsset(const std::string &path) const override;
  std::vector<Uint8> readAsset(const std::string &path) const override;
  std::vector<std::string> listAsset(const std::string &prefix) const override;
  std::string localPathAsset(const std::string &path) const override;
  std::unique_ptr<Kotonoha::AssetStream>
  openStream(const std::string &path) const override;

  // Resolves a logical path (handling `\` -> `/`, case-insensitive match, and
  // `sound/<dir>/<clip>` -> `voice/<dir>/<clip>` fallback). Returns empty if
  // not found.
  std::string resolveActualPath(std::string_view requestedPath) const;

private:
  std::filesystem::path rootDirectory_;
  std::shared_ptr<Kotonoha::Filesystem::Filesystem> vfs_;
  std::unordered_map<std::string, std::string> caseIndex_; // lower -> actual VFS path
  std::vector<std::shared_ptr<ArchiveReader>> archives_;
};

} // namespace Ponscripter
