#pragma once
// Disc asset adapter shared independently of the Amagami runtime.
#include <Amagami/Source.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
namespace Kotonoha::Providers {
class AmagamiAssets : public Kotonoha::DiscImageAssetProvider {
public:
  explicit AmagamiAssets(std::shared_ptr<Amagami::Source> source);
  std::string name() const override { return "amagami-disc"; }
  std::shared_ptr<Amagami::Source> source() const { return source_; }
  struct Fragment {
    SDL_FRect destination, source;
    uint32_t group;
  };
  const std::vector<Fragment> &fragments(const std::string &) const;
  SDL_Rect bodyBounds(const std::string &) const;

protected:
  Kotonoha::AudioClip loadAudioAsset(const std::string &) const override;
  bool containsAsset(const std::string &) const override;
  std::vector<Uint8> readAsset(const std::string &) const override;
  Kotonoha::SurfacePtr loadSurfaceAsset(const std::string &) const override;

private:
  std::shared_ptr<Amagami::Source> source_;
  mutable std::map<std::string, std::vector<Fragment>> meshes_;
};
} // namespace Kotonoha::Providers
