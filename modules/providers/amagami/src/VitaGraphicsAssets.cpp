#include <Amagami/VitaGraphicsAssets.hpp>
#include <Amagami/VitaGraphics.hpp>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Kotonoha::Providers {
VitaGraphicsAssets::VitaGraphicsAssets(std::string graphCpkPath)
    : cpkPath_(std::move(graphCpkPath)),
      index_(Amagami::Vita::cpk_read_index(cpkPath_)) {}

bool VitaGraphicsAssets::containsAsset(const std::string &id) const {
  return index_.find(id) != index_.end();
}

std::vector<Uint8>
VitaGraphicsAssets::readAsset(const std::string &id) const {
  std::string raw;
  if (!Amagami::Vita::cpk_read(cpkPath_, index_, id, raw))
    throw std::out_of_range("Amagami Vita graphic not found: " + id);
  return std::vector<Uint8>(raw.begin(), raw.end());
}

std::vector<std::string>
VitaGraphicsAssets::listAsset(const std::string &prefix) const {
  std::vector<std::string> names;
  for (const auto &entry : index_)
    if (entry.first.rfind(prefix, 0) == 0)
      names.push_back(entry.first);
  std::sort(names.begin(), names.end());
  return names;
}

Kotonoha::SurfacePtr
VitaGraphicsAssets::loadSurfaceAsset(const std::string &id) const {
  if (!Amagami::Vita::pictureLayout(id))
    return Kotonoha::AssetProvider::loadSurfaceAsset(id);
  const auto raw = read(id); // Respects engine asset overlays.
  const std::string_view bytes(reinterpret_cast<const char *>(raw.data()),
                               raw.size());
  const auto image = Amagami::Vita::decodeVitaPicture(id, bytes);
  auto *view = SDL_CreateSurfaceFrom(
      int(image.width), int(image.height), SDL_PIXELFORMAT_RGBA32,
      const_cast<Uint8 *>(image.rgba.data()), int(image.width) * 4);
  if (!view)
    throw std::runtime_error(SDL_GetError());
  auto *copy = SDL_DuplicateSurface(view);
  SDL_DestroySurface(view);
  if (!copy)
    throw std::runtime_error(SDL_GetError());
  if (!SDL_SetSurfaceBlendMode(copy, SDL_BLENDMODE_BLEND)) {
    SDL_DestroySurface(copy);
    throw std::runtime_error(SDL_GetError());
  }
  return Kotonoha::SurfacePtr(copy);
}
} // namespace Kotonoha::Providers
