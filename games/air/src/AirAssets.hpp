#pragma once
#include <Air/Config.hpp>
#include <Air/Script.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>
#include <map>
namespace Air {
class AirAssets final : public Kotonoha::AssetProvider {
  std::unique_ptr<Kotonoha::AssetProvider> source;
  std::filesystem::path root;
  std::string configSource;
  Config config;

public:
  explicit AirAssets(const std::filesystem::path &directory,
                     const std::filesystem::path &overlay = {});
  std::string name() const override { return "AIR RealLive data"; }
  Kotonoha::SurfacePtr loadSurfaceAsset(const std::string &) const override;
  Kotonoha::AudioClip loadAudioAsset(const std::string &) const override;
  std::string graphic(const std::string &name) const;
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string> listAsset(const std::string &prefix) const override;
  std::string localPathAsset(const std::string &id) const override;
  ScriptArchive scripts() const;
  const auto &directory() const { return root; }
  const Config &settings() const { return config; }
  const std::string &settingsSource() const { return configSource; }
};
} // namespace Air
