#pragma once

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
#include <Pmail/GcPack.hpp>
#include <Pmail/LargePonPonCodecs.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::Providers {

struct LargePonPonFontProfile {
  std::string name;
  int weight = 400;
  SDL_Color color{240, 240, 232, 255};
};

struct LargePonPonConfig {
  std::array<int16_t, 64> config{};
  std::array<std::string, 4> names{"伊能", "歩", "いのう", "あゆむ"};
  std::array<LargePonPonFontProfile, 21> fonts{};
};

class LargePonPonAssets : public AssetProvider {
public:
  explicit LargePonPonAssets(const std::filesystem::path &directory);
  explicit LargePonPonAssets(
      std::shared_ptr<Kotonoha::Filesystem::Filesystem> source);

  std::string name() const override { return "LargePonPon"; }
  bool containsAsset(const std::string &id) const override;
  std::vector<Uint8> readAsset(const std::string &id) const override;
  std::vector<std::string>
  listAsset(const std::string &prefix = {}) const override;
  std::string localPathAsset(const std::string &id) const override;

  SurfacePtr loadSurfaceAsset(const std::string &id) const override;
  AudioClip loadAudioAsset(const std::string &id) const override;

  // Typed helpers for Large PonPon multi-frame and specialized formats.
  const LargePonPonConfig &config() const { return config_; }
  const GcPackArchive &scriptPack() const { return scriptPack_; }
  const GcPackArchive &graphicPack() const { return graphicPack_; }
  const GcPackArchive &soundPack() const { return soundPack_; }

  std::vector<uint8_t> loadScriptBytes(const std::string &stemOrName) const;
  SurfacePtr loadCrgb(const std::string &stem) const;
  SurfacePtr loadBgd(const std::string &stem) const;
  SurfacePtr loadChar(const std::string &stem) const;
  std::vector<SurfacePtr> loadPchrFrames(const std::string &stem) const;
  std::vector<SurfacePtr> loadIconFrames(const std::string &stem) const;
  std::vector<SurfacePtr> loadPrgbSprites(const std::string &stem) const;
  SurfacePtr loadP256Thumbnail(const std::string &stem, size_t index,
                               bool color) const;
  CanmData loadCanm(const std::string &stem) const;
  std::vector<uint8_t> loadAlphaMask(const std::string &stem,
                                     bool isAlpm) const;
  Adp4Decoded loadAdp4(const std::string &stem) const;
  AudioClip loadSmfdAudio(const std::string &stem) const;

private:
  void initFromSource();
  void parseMainSysIni();
  static std::string ensureExt(const std::string &stem, const char *ext);
  static std::string upperExt(const std::string &id);

  std::shared_ptr<Kotonoha::Filesystem::Filesystem> source_;
  GcPackArchive scriptPack_;
  GcPackArchive graphicPack_;
  GcPackArchive soundPack_;
  LargePonPonConfig config_;
};

} // namespace Kotonoha::Providers
