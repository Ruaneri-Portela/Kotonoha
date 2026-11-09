#pragma once
#include <RealLive/Config.hpp>
#include <RealLive/Script.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>
#include <map>
namespace RealLive {
class RealLiveAssets final : public Kotonoha::AssetProvider {
  std::unique_ptr<Kotonoha::AssetProvider> source;
  std::filesystem::path root;
  std::string configSource;
  Config config;

public:
  explicit RealLiveAssets(const std::filesystem::path &directory,
                     const std::filesystem::path &overlay = {});
  std::string name() const override { return "REALLIVE RealLive data"; }
  Kotonoha::SurfacePtr loadSurfaceAsset(const std::string &) const override;
  Kotonoha::AudioClip loadAudioAsset(const std::string &) const override;
  std::string graphic(const std::string &name, bool warn = true) const;
  std::string animation(const std::string &name) const;
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string> listAsset(const std::string &prefix) const override;
  std::string localPathAsset(const std::string &id) const override;
  ScriptArchive scripts() const;
  const auto &directory() const { return root; }
  const Config &settings() const { return config; }
  const std::string &settingsSource() const { return configSource; }
};
} // namespace RealLive
