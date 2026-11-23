#include <Amagami/Assets.hpp>
#include <Kotonoha/Formats/CriAudio.hpp>
#include <Kotonoha/Formats/Ps2Tim2.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace Kotonoha::Providers {
using namespace Amagami;
AmagamiAssets::AmagamiAssets(std::shared_ptr<Source> s)
    : DiscImageAssetProvider(s->disc()), source_(std::move(s)) {}
const std::vector<AmagamiAssets::Fragment> &
AmagamiAssets::fragments(const std::string &file) const {
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
SDL_Rect AmagamiAssets::bodyBounds(const std::string &file) const {
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
std::vector<std::string>
AmagamiAssets::listAsset(const std::string &prefix) const {
  // The ISO directory only lists archive containers. Expose virtual asset
  // paths that the Amagami VM actually uses, without reading audio payloads.
  auto result = DiscImageAssetProvider::listAsset(prefix);
  const auto append = [&](const std::string &id) {
    if (prefix.empty() || id.rfind(prefix, 0) == 0)
      result.push_back(id);
  };
  for (const auto &id : source_->voiceAssetIds())
    append(id);
  for (unsigned number = 0; number <= 80; ++number) {
    if (source_->musicIndex(number) >= 0)
      append("music/" + std::to_string(number));
    if (source_->musicIndex(number, true) >= 0)
      append("music/classic/" + std::to_string(number));
  }
  for (std::size_t i = 0; i < source_->soundEffectCount(); ++i)
    append("se/" + std::to_string(i));
  const auto &ambience = source_->archive("SOUND/SOUND.AFS");
  for (std::size_t i = 0; i < ambience.entries.size(); ++i)
    append("sound/" + std::to_string(i));
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

bool AmagamiAssets::containsAsset(const std::string &id) const {
  if (id.rfind("actor-body/", 0) == 0)
    return contains(id.substr(11));
  if (id.rfind("voice/", 0) == 0 || id.rfind("music/", 0) == 0)
    return true;
  if (id.rfind("se/", 0) == 0) {
    try {
      size_t used = 0;
      const auto index = std::stoul(id.substr(3), &used);
      return used == id.size() - 3 &&
             (index < source_->soundEffectCount() ||
              (index >= 12 && index - 12 < source_->soundEffectCount()));
    } catch (const std::exception &) {
      return false;
    }
  }
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
std::vector<Uint8> AmagamiAssets::readAsset(const std::string &id) const {
  if (id.rfind("voice/", 0) == 0) {
    auto slash = id.find('/', 6);
    if (slash == std::string::npos)
      throw std::runtime_error("Invalid voice ID");
    const auto rawIndex = std::stoul(id.substr(slash + 1));
    if (rawIndex > std::numeric_limits<unsigned>::max())
      throw std::runtime_error("Voice index out of range: " + id);
    auto result = source_->voice(id.substr(6, slash - 6), unsigned(rawIndex));
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
  if (id.rfind("se/", 0) == 0) {
    size_t used = 0;
    const auto index = std::stoul(id.substr(3), &used);
    if (used != id.size() - 3)
      throw std::runtime_error("Invalid sound effect ID");
    return source_->soundEffect(unsigned(index)).adpcm;
  }
  if (id.rfind("music/classic/", 0) == 0) {
    const auto rawIndex = std::stoul(id.substr(14));
    if (rawIndex > std::numeric_limits<unsigned>::max())
      throw std::runtime_error("Unknown Amagami classic music number");
    int index = source_->musicIndex(unsigned(rawIndex), true);
    if (index < 0)
      throw std::runtime_error("Unknown Amagami classic music number");
    return source_->read("SOUND/MUSIC.AFS", index);
  }
  if (id.rfind("music/", 0) == 0) {
    const auto rawIndex = std::stoul(id.substr(6));
    if (rawIndex > std::numeric_limits<unsigned>::max())
      throw std::runtime_error("Unknown Amagami music number");
    int index = source_->musicIndex(unsigned(rawIndex));
    if (index < 0)
      throw std::runtime_error("Unknown Amagami music number");
    return source_->read("SOUND/MUSIC.AFS", index);
  }
  if (auto location = source_->graphic(id))
    return source_->read(location->archive, location->index);
  return DiscImageAssetProvider::readAsset(id);
}
Kotonoha::SurfacePtr
AmagamiAssets::loadSurfaceAsset(const std::string &id) const {
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
} // namespace Kotonoha::Providers

namespace Kotonoha::Providers {
Kotonoha::AudioClip AmagamiAssets::loadAudioAsset(const std::string &id) const {
  if (id.rfind("se/", 0) == 0) {
    size_t used = 0;
    const auto index = std::stoul(id.substr(3), &used);
    if (used != id.size() - 3)
      throw std::runtime_error("Invalid sound effect ID");
    const auto se = source_->soundEffect(unsigned(index));
    static constexpr int kF0[5] = {0, 60, 115, 98, 122};
    static constexpr int kF1[5] = {0, 0, -52, -55, -60};
    std::vector<int16_t> pcm;
    pcm.reserve((se.adpcm.size() / 16) * 28);
    int32_t s0 = 0, s1 = 0;
    for (size_t off = 0; off + 16 <= se.adpcm.size(); off += 16) {
      const uint8_t *blk = se.adpcm.data() + off;
      int shift = blk[0] & 0x0f;
      if (shift > 12)
        shift = 9;
      int filter = (blk[0] >> 4) & 0x07;
      if (filter > 4)
        filter = 0;
      const uint8_t flags = blk[1];
      if (flags == 7)
        break;
      for (int i = 2; i < 16; ++i) {
        const uint8_t byte = blk[i];
        for (int n = 0; n < 2; ++n) {
          const int nibble = (n == 0) ? (byte & 0x0f) : ((byte >> 4) & 0x0f);
          int32_t sample = int32_t(int16_t(uint16_t(nibble) << 12) >> shift);
          sample += (s0 * kF0[filter] + s1 * kF1[filter] + 32) >> 6;
          sample = std::clamp(sample, -32768, 32767);
          s1 = s0;
          s0 = sample;
          pcm.push_back(int16_t(sample));
        }
      }
      if (flags & 1)
        break;
    }
    Kotonoha::AudioClip clip;
    clip.spec = SDL_AudioSpec{SDL_AUDIO_S16, 1, se.sampleRate};
    clip.data.resize(pcm.size() * sizeof(int16_t));
    if (!pcm.empty())
      std::memcpy(clip.data.data(), pcm.data(), clip.data.size());
    return clip;
  }
  if (id.rfind("voice/", 0) == 0 || id.rfind("music/", 0) == 0 ||
      id.rfind("sound/", 0) == 0)
    return Kotonoha::decodeCriAudio(readAsset(id), {0x5a17, 0x509f, 0x5bfd});
  return Kotonoha::AssetProvider::loadAudioAsset(id);
}
} // namespace Kotonoha::Providers
