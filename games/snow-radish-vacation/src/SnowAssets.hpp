#pragma once

#include "SnowRadish/Pack.hpp"

#include <Kotonoha/core/AssetProvider.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace SnowRadish {

// The four retail packs (SCRIPT / GRAPHIC / SOUND / VOICE.DAT, any case) as the
// engine AssetProvider. Identifiers:
//   script                 raw SCRIPT.DAT
//   graphic/<name.ext>     GRAPHIC.DAT entry (evd, img, chd, anm, alpf, alpa, snw)
//   graphic/<name>.anm#<n> frame n of an animation
//   sound/<stem>           SOUND.DAT ADPS track (BGM / SE)
//   voice/<stem>           VOICE.DAT ADP4 voice
// The decoders turn EVD / IMG / CHD / ANM into RGBA surfaces and ADPS / ADP4
// into 16-bit PCM, so engine consumers (textures, upscaling, audio) just work.
class SnowAssets : public Kotonoha::AssetProvider {
public:
  // Placement of a CHD sprite or ANM frame on the 800x600 canvas.
  struct Placement {
    float x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
  };
  struct AnimationInfo {
    std::vector<Placement> frames;
    std::vector<Animation::Region> regions;
    bool cursor = false;
  };

  explicit SnowAssets(std::filesystem::path directory);

  const std::filesystem::path &Directory() const { return directory_; }

  std::string name() const override;
  bool contains(const std::string &id) const override;
  std::vector<Uint8> read(const std::string &id) const override;
  std::vector<std::string> list(const std::string &prefix = {}) const override;
  Kotonoha::SurfacePtr loadSurface(const std::string &id) const override;
  Kotonoha::AudioClip loadAudio(const std::string &id) const override;

  // Typed access for the presentation layer (decoded once, cached).
  std::vector<uint8_t> Script() const { return read("script"); }
  bool HasGraphic(const std::string &file) const;
  Placement Sprite(const std::string &name) const;
  const AnimationInfo &AnimationOf(const std::string &name) const;
  bool Alpha(const std::string &file, bool alternate, AlphaMask &out) const;
  bool Adps(const std::string &stem, AdpsTrack &out) const;
  Pcm Voice(const std::string &stem) const;
  std::vector<uint8_t> Graphic(const std::string &file) const;

  static std::string Lower(std::string s);

private:
  struct Pack {
    std::filesystem::path path;
    std::map<std::string, PackEntry> index;
  };
  const Pack *PackOf(const std::string &id, std::string &key) const;
  static std::vector<uint8_t> Extract(const Pack &pack, const PackEntry &entry);

  std::filesystem::path directory_;
  std::filesystem::path scriptPath_;
  Pack graphic_, sound_, voice_;
  mutable std::map<std::string, Placement> sprites_;
  mutable std::map<std::string, AnimationInfo> animations_;
};

} // namespace SnowRadish
