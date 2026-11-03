#pragma once
#include <Air/Config.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>
#include <map>
namespace Air {
class AirAssets final : public Kotonoha::IndexedFileSystemAssetProvider {
  std::string configSource;
  Config config;

public:
  explicit AirAssets(const std::filesystem::path &directory,
                     const std::filesystem::path &overlay = {});
  std::string name() const override { return "AIR RealLive data"; }
  Kotonoha::SurfacePtr loadSurfaceAsset(const std::string &) const override;
  Kotonoha::AudioClip loadAudioAsset(const std::string &) const override;
  std::string graphic(const std::string &name) const;
  const auto &directory() const { return getRoot(); }
  const Config &settings() const { return config; }
  const std::string &settingsSource() const { return configSource; }
};
} // namespace Air
