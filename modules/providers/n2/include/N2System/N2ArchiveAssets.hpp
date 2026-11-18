// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <N2System/Archive.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace N2System {
// Adapter to Kotonoha's asset pipeline. User-supplied archives remain read-only.
class N2ArchiveAssets final : public Kotonoha::AssetProvider {
public:
  explicit N2ArchiveAssets(std::string_view rootDir = "");

  std::string name() const override { return "N2System archives"; }
  void setRootDirectory(std::string_view rootDir);
  const std::filesystem::path &rootDirectory() const { return rootDirectory_; }

  void mount(std::vector<std::uint8_t> data);
  void addAfsNames(std::string_view ini);
  bool containsAsset(const std::string& path) const override;
  std::vector<Uint8> readAsset(const std::string& path) const override;
  std::vector<std::string> listAsset(const std::string& prefix) const override;

  std::unique_ptr<Kotonoha::AssetStream>
  openStream(const std::string& path) const override;

private:
  std::filesystem::path resolveFsPath(std::string_view path) const;

  std::filesystem::path rootDirectory_;
  std::vector<std::shared_ptr<Archive>> archives_;
};
} // namespace N2System
