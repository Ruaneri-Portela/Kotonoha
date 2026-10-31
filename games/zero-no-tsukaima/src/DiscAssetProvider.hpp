#pragma once

#include <Kotonoha/core/DiscImage.hpp>
#include <ZeroNoTsukaima/Source.hpp>

#include <memory>
#include <string>

namespace ZeroNoTsukaima {

// The retail disc image as the engine asset source. Identifiers:
//   scenedat/<index>  SCENEDAT picture (TIM2)     normal/<NAME>  NORMAL picture (TIM2)
//   voice/<index>     VOICE stream (VAG)          sound/<NAME>   SOUND stream (VAG)
//   movie/<number>    MOVIE .PSS (MPEG-2 video)   script/<NAME>  scene script (KS)
//   movie/<number>.wav  the PSS soundtrack (SShd PCM, which FFmpeg skips) as WAV
// Any other id is an ISO path ("SYSTEM.CNF", "MOVIE/ZERO2OP.PSS") served by the
// engine disc provider. Pictures decode from TIM2, so engine textures
// (texture()) work on every picture id.
class DiscAssetProvider : public Kotonoha::DiscImageAssetProvider {
public:
  explicit DiscAssetProvider(const std::string &discPath);

  const SceneSource &Source() const { return *source_; }
  const std::string &DiscPath() const { return path_; }

  std::string name() const override;
  bool contains(const std::string &id) const override;
  std::vector<Uint8> read(const std::string &id) const override;
  std::vector<std::string> list(const std::string &prefix = {}) const override;
  Kotonoha::SurfacePtr loadSurface(const std::string &id) const override;

private:
  DiscAssetProvider(std::shared_ptr<SceneSource> source, const std::string &discPath);
  bool Read(const std::string &id, std::vector<uint8_t> *out) const;

  std::shared_ptr<SceneSource> source_;
  std::string path_;
};

} // namespace ZeroNoTsukaima
