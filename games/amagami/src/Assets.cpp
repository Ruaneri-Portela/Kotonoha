#include "Assets.hpp"
#include <Kotonoha/media/CriAudio.hpp>
#include <Kotonoha/media/Ps2Tim2.hpp>
#include <stdexcept>
namespace Amagami {
Assets::Assets(std::shared_ptr<Source> s)
    : DiscImageAssetProvider(s->disc()), source_(std::move(s)) {}
bool Assets::containsAsset(const std::string &id) const {
  if (id.rfind("voice/", 0) == 0 || id.rfind("music/", 0) == 0)
    return true;
  for (const auto &a :
       {"GRAPH/GRAPH0.ARC", "GRAPH/GRAPH1.ARC", "GRAPH/GRAPH2.ARC"})
    if (source_->find(a, id) >= 0)
      return true;
  return DiscImageAssetProvider::containsAsset(id);
}
std::vector<Uint8> Assets::readAsset(const std::string &id) const {
  if (id.rfind("voice/", 0) == 0) {
    auto slash = id.find('/', 6);
    if (slash == std::string::npos)
      throw std::runtime_error("Invalid voice ID");
    auto result = source_->voice(id.substr(6, slash - 6),
                                 std::stoul(id.substr(slash + 1)));
    if (result.empty())
      throw std::runtime_error("Voice not found: " + id);
    return result;
  }
  if (id.rfind("music/", 0) == 0) {
    int index = source_->musicIndex(std::stoul(id.substr(6)));
    if (index < 0)
      throw std::runtime_error("Unknown Amagami music number");
    return source_->read("SOUND/MUSIC.AFS", index);
  }
  for (const auto &a :
       {"GRAPH/GRAPH0.ARC", "GRAPH/GRAPH1.ARC", "GRAPH/GRAPH2.ARC"}) {
    int n = source_->find(a, id);
    if (n >= 0)
      return source_->read(a, n);
  }
  return DiscImageAssetProvider::readAsset(id);
}
Kotonoha::SurfacePtr Assets::loadSurfaceAsset(const std::string &id) const {
  if (id.size() < 4 || id.substr(id.size() - 4) != ".tm2")
    return AssetProvider::loadSurfaceAsset(id);
  auto bytes = readAsset(id);
  Kotonoha::Ps2Image image;
  if (!Kotonoha::DecodePs2Tim2(bytes.data(), bytes.size(), &image))
    throw std::runtime_error("Invalid TIM2: " + id);
  auto *view =
      SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
                            image.rgba.data(), image.width * 4);
  if (!view)
    throw std::runtime_error(SDL_GetError());
  auto *copy = SDL_DuplicateSurface(view);
  SDL_DestroySurface(view);
  if (!copy)
    throw std::runtime_error(SDL_GetError());
  return Kotonoha::SurfacePtr(copy);
}
} // namespace Amagami

namespace Amagami {
Kotonoha::AudioClip Assets::loadAudioAsset(const std::string &id) const {
  if (id.rfind("voice/", 0) == 0 || id.rfind("music/", 0) == 0)
    return Kotonoha::decodeCriAudio(readAsset(id), {0x5a17, 0x509f, 0x5bfd});
  return Kotonoha::AssetProvider::loadAudioAsset(id);
}
} // namespace Amagami
