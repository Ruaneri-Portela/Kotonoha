#include <Amagami/VitaGraphics.hpp>
#include <Amagami/VitaGraphicsAssets.hpp>
#include <algorithm>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <stdexcept>
#include <utility>

namespace Kotonoha::Providers {
namespace {
Kotonoha::SurfacePtr fromRgba(const Amagami::Vita::Picture &image) {
  if (image.width == 0 || image.height == 0 ||
      image.rgba.size() != uint64_t(image.width) * image.height * 4)
    throw std::runtime_error("Amagami Vita: invalid RGBA surface dimensions");
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
std::string readElf(const std::string &path) {
  const auto bytes = Kotonoha::Filesystem::readLocalFile(path, 32u * 1024u * 1024u);
  return std::string(bytes.begin(), bytes.end());
}
} // namespace

VitaGraphicsAssets::VitaGraphicsAssets(std::string graphCpkPath,
                                       std::string ebootElfPath)
    : cpkPath_(std::move(graphCpkPath)),
      index_(Amagami::Vita::cpk_read_index(cpkPath_)) {
  if (!ebootElfPath.empty()) {
    std::string sysgraph;
    if (!Amagami::Vita::cpk_read(cpkPath_, index_, "system/sysgraph.tm2",
                                 sysgraph))
      throw std::runtime_error("Amagami Vita: sysgraph missing from CPK");
    ui_ = std::make_unique<Amagami::Vita::UiAtlas>(readElf(ebootElfPath),
                                                   std::move(sysgraph));
  }
}

Kotonoha::SurfacePtr VitaGraphicsAssets::uiSurface(uint32_t textureId,
                                                   uint32_t variant) const {
  if (!ui_)
    throw std::runtime_error(
        "Amagami Vita: a plain eboot ELF is needed for UI textures");
  const auto picture = ui_->decode(textureId, variant);
  return picture ? fromRgba(*picture) : Kotonoha::SurfacePtr{};
}

Kotonoha::SurfacePtr VitaGraphicsAssets::fontGlyph(unsigned font,
                                                   uint16_t shiftJis,
                                                   unsigned palette) const {
  if (font > 1)
    throw std::out_of_range("Amagami Vita: font selector out of range");
  const char *names[] = {"font/AMV_font18_21.tm2", "font/AMV_font18_21im.tm2"};
  std::lock_guard<std::mutex> guard(fontMutex_);
  std::string &bytes = fontData_[font];
  if (bytes.empty() &&
      !Amagami::Vita::cpk_read(cpkPath_, index_, names[font], bytes))
    throw std::runtime_error("Amagami Vita: font atlas not in GRAPH.CPK");
  return fromRgba(Amagami::Vita::decodeFontGlyph(bytes, shiftJis, palette));
}

bool VitaGraphicsAssets::containsAsset(const std::string &id) const {
  return index_.find(id) != index_.end();
}

std::vector<Uint8> VitaGraphicsAssets::readAsset(const std::string &id) const {
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
  return fromRgba(image);
}
} // namespace Kotonoha::Providers
