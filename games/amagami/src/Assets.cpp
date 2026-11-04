#include "Assets.hpp"
#include <Kotonoha/media/CriAudio.hpp>
#include <Kotonoha/media/Ps2Tim2.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace Amagami {
Assets::Assets(std::shared_ptr<Source> s)
    : DiscImageAssetProvider(s->disc()), source_(std::move(s)) {}
const std::vector<Assets::Fragment> &
Assets::fragments(const std::string &file) const {
  const auto mesh = file.substr(0, file.size() - 5) + "z.zb";
  const auto key = overlayDirectory().string() + "|" + mesh;
  if (auto cached = meshes_.find(key); cached != meshes_.end())
    return cached->second;
  const auto bytes = read(mesh);
  if (bytes.empty() || bytes.size() % 32 || bytes.size() > 1024 * 1024)
    throw std::runtime_error("Invalid actor mesh: " + mesh);
  std::vector<Fragment> result;
  for (size_t i = 0; i < bytes.size(); i += 32) {
    float p[6];
    uint32_t group;
    std::memcpy(p, bytes.data() + i, sizeof(p));
    std::memcpy(&group, bytes.data() + i + 24, 4);
    if (!std::all_of(
            std::begin(p), std::end(p),
            [](float n) { return std::isfinite(n) && std::abs(n) < 16384; }) ||
        p[4] <= 0 || p[5] <= 0)
      throw std::runtime_error("Invalid actor fragment coordinates");
    result.push_back(
        {{p[0], p[1], p[4], p[5]}, {p[2], p[3], p[4], p[5]}, group});
  }
  return meshes_.emplace(key, std::move(result)).first->second;
}
SDL_Rect Assets::bodyBounds(const std::string &file) const {
  const auto &parts = fragments(file);
  float x = parts.front().destination.x, y = parts.front().destination.y,
        right = x, bottom = y;
  for (const auto &part : parts) {
    x = std::min(x, part.destination.x);
    y = std::min(y, part.destination.y);
    right = std::max(right, part.destination.x + part.destination.w);
    bottom = std::max(bottom, part.destination.y + part.destination.h);
  }
  return {int(std::floor(x)), int(std::floor(y)),
          int(std::ceil(right) - std::floor(x)),
          int(std::ceil(bottom) - std::floor(y))};
}
bool Assets::containsAsset(const std::string &id) const {
  if (id.rfind("actor-body/", 0) == 0)
    return contains(id.substr(11));
  if (id.rfind("voice/", 0) == 0 || id.rfind("music/", 0) == 0)
    return true;
  if (id.rfind("sound/", 0) == 0) {
    try {
      size_t used = 0;
      const auto index = std::stoul(id.substr(6), &used);
      return used == id.size() - 6 &&
             index < source_->archive("SOUND/SOUND.AFS").entries.size();
    } catch (const std::exception &) {
      return false;
    }
  }
  if (id.rfind("actor-face/", 0) == 0) {
    const auto slash = id.rfind('/');
    return slash > 11 && contains(id.substr(11, slash - 11));
  }
  if (source_->graphic(id))
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
  if (id.rfind("sound/", 0) == 0) {
    size_t used = 0;
    const auto index = std::stoul(id.substr(6), &used);
    if (used != id.size() - 6)
      throw std::runtime_error("Invalid ambient sound ID");
    return source_->ambience(unsigned(index));
  }
  if (id.rfind("music/", 0) == 0) {
    int index = source_->musicIndex(std::stoul(id.substr(6)));
    if (index < 0)
      throw std::runtime_error("Unknown Amagami music number");
    return source_->read("SOUND/MUSIC.AFS", index);
  }
  if (auto location = source_->graphic(id))
    return source_->read(location->archive, location->index);
  return DiscImageAssetProvider::readAsset(id);
}
Kotonoha::SurfacePtr Assets::loadSurfaceAsset(const std::string &id) const {
  if (id.rfind("actor-face/", 0) == 0) {
    const auto slash = id.rfind('/');
    if (slash <= 11)
      throw std::runtime_error("Invalid face tile ID");
    const auto file = id.substr(11, slash - 11);
    const auto &part = fragments(file).at(std::stoul(id.substr(slash + 1)));
    auto atlas = loadSurface(file);
    SDL_Rect crop{int(part.source.x), int(part.source.y), int(part.source.w),
                  int(part.source.h)};
    Kotonoha::SurfacePtr tile(
        SDL_CreateSurface(crop.w, crop.h, SDL_PIXELFORMAT_RGBA32));
    if (!tile || !SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_NONE) ||
        !SDL_BlitSurface(atlas.get(), &crop, tile.get(), nullptr))
      throw std::runtime_error(SDL_GetError());
    SDL_SetSurfaceBlendMode(tile.get(), SDL_BLENDMODE_BLEND);
    return tile;
  }

  if (id.rfind("actor-body/", 0) == 0) {
    const auto file = id.substr(11);
    const auto bounds = bodyBounds(file);
    auto atlas = loadSurface(file);
    Kotonoha::SurfacePtr composed(
        SDL_CreateSurface(bounds.w, bounds.h, SDL_PIXELFORMAT_RGBA32));
    if (!composed || !SDL_ClearSurface(composed.get(), 0, 0, 0, 0) ||
        !SDL_SetSurfaceBlendMode(atlas.get(), SDL_BLENDMODE_BLEND))
      throw std::runtime_error(SDL_GetError());
    for (const auto &part : fragments(file)) {
      SDL_Rect src{int(part.source.x), int(part.source.y), int(part.source.w),
                   int(part.source.h)};
      SDL_Rect dst{int(part.destination.x) - bounds.x,
                   int(part.destination.y) - bounds.y, src.w, src.h};
      if (!SDL_BlitSurface(atlas.get(), &src, composed.get(), &dst))
        throw std::runtime_error(SDL_GetError());
    }
    SDL_SetSurfaceBlendMode(composed.get(), SDL_BLENDMODE_BLEND);
    return composed;
  }
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
  SDL_SetSurfaceBlendMode(copy, SDL_BLENDMODE_BLEND);
  return Kotonoha::SurfacePtr(copy);
}
} // namespace Amagami

namespace Amagami {
Kotonoha::AudioClip Assets::loadAudioAsset(const std::string &id) const {
  if (id.rfind("voice/", 0) == 0 || id.rfind("music/", 0) == 0 ||
      id.rfind("sound/", 0) == 0)
    return Kotonoha::decodeCriAudio(readAsset(id), {0x5a17, 0x509f, 0x5bfd});
  return Kotonoha::AssetProvider::loadAudioAsset(id);
}
} // namespace Amagami
