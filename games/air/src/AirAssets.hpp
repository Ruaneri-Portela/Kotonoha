#pragma once
#include <Air/Config.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <filesystem>
#include <map>
namespace Air {
class AirAssets final : public Kotonoha::AssetProvider {
  std::filesystem::path root;
  std::map<std::string, std::filesystem::path> files;
  std::string configSource;
  Config config;

public:
  explicit AirAssets(const std::filesystem::path &directory);
  std::string name() const override { return "AIR RealLive data"; }
  bool contains(const std::string &) const override;
  std::vector<Uint8> read(const std::string &) const override;
  std::string localPath(const std::string &) const override;
  std::vector<std::string> list(const std::string &prefix = {}) const override;
  Kotonoha::SurfacePtr loadSurface(const std::string &) const override;
  Kotonoha::AudioClip loadAudio(const std::string &) const override;
  std::string graphic(const std::string &name) const;
  const auto &directory() const { return root; }
  const Config &settings() const { return config; }
  const std::string &settingsSource() const { return configSource; }
};
} // namespace Air
