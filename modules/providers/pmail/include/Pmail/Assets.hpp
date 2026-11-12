#pragma once
#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::Providers {

class PmailAssets : public AssetProvider {
public:
  explicit PmailAssets(const std::filesystem::path &directory);

  std::string name() const override;
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string> listAsset(const std::string &prefix = {}) const override;
  std::string localPathAsset(const std::string &id) const override;

  SurfacePtr loadSurfaceAsset(const std::string &id) const override;
  AudioClip loadAudioAsset(const std::string &id) const override;

private:
  std::filesystem::path root_;
};

} // namespace Kotonoha::Providers
