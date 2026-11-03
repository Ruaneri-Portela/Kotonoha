#pragma once
#include <Amagami/Source.hpp>
#include <Kotonoha/core/AssetProvider.hpp>
namespace Amagami {
class Assets : public Kotonoha::DiscImageAssetProvider {
public:
  explicit Assets(std::shared_ptr<Source> source);
  std::string name() const override { return "amagami-disc"; }
  std::shared_ptr<Source> source() const { return source_; }

protected:
  Kotonoha::AudioClip loadAudioAsset(const std::string &) const override;
  bool containsAsset(const std::string &) const override;
  std::vector<Uint8> readAsset(const std::string &) const override;
  Kotonoha::SurfacePtr loadSurfaceAsset(const std::string &) const override;

private:
  std::shared_ptr<Source> source_;
};
} // namespace Amagami
