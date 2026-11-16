// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <N2System/Archive.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
namespace N2System {
// Adapter to Kotonoha's asset pipeline. User-supplied archives remain read-only.
class N2ArchiveAssets final : public Kotonoha::AssetProvider {
public:
  std::string name() const override { return "N2System archives"; }
  void mount(std::vector<std::uint8_t> data);
  bool containsAsset(const std::string& path) const override;
  std::vector<Uint8> readAsset(const std::string& path) const override;
  std::vector<std::string> listAsset(const std::string& prefix) const override;
private:
  std::vector<Archive> archives_;
};
} // namespace N2System
