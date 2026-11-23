#include "PmailExtrasMenu.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <Pmail/Port.hpp>

namespace Kotonoha::PmailPort {
namespace {
constexpr int kStageWidth = 800;
constexpr int kStageHeight = 600;

const std::array<const char *, 4> kItemGraphics{"OMAMENU01a", "OMAMENU01b",
                                                "OMAMENU01c", "OMAMENU01d"};
} // namespace

PmailExtrasMenu::PmailExtrasMenu(Kotonoha::Engine &engine, PmailGame &game,
                                 Kotonoha::Providers::SnowAssets &assets)
    : engine_(engine), game_(game), assets_(assets) {}

bool PmailExtrasMenu::loadMap() const {
  if (mapLoaded_)
    return !map_.empty();
  mapLoaded_ = true;
  try {
    map_ = assets_.readAsset("graphic/OMAMENU00.MAP");
  } catch (...) {
    map_.clear();
  }
  if (map_.size() != size_t(kStageWidth) * kStageHeight)
    map_.clear();
  return !map_.empty();
}

int PmailExtrasMenu::hitTest(float x, float y) const {
  if (!loadMap())
    return 0;
  const int px = int(x);
  const int py = int(y);
  if (px < 0 || px >= kStageWidth || py < 0 || py >= kStageHeight)
    return 0;
  const uint8_t value = map_[size_t(py) * kStageWidth + size_t(px)];
  // 0 is the background, 0xff marks unused pixels outside the menu mask.
  return value >= 1 && value <= 4 ? int(value) : 0;
}

SDL_Texture *PmailExtrasMenu::loadTexture(Kotonoha::RendererRef renderer,
                                          const std::string &key,
                                          const std::string &file) {
  auto found = textures_.find(key);
  if (found != textures_.end())
    return found->second.tex.get();

  Texture texture;
  try {
    auto surface = assets_.loadSurfaceAsset("graphic/" + file);
    if (surface) {
      texture.tex.reset(SDL_CreateTextureFromSurface(&renderer, surface.get()));
      if (texture.tex) {
        SDL_GetTextureSize(texture.tex.get(), &texture.w, &texture.h);
        SDL_SetTextureBlendMode(texture.tex.get(), SDL_BLENDMODE_BLEND);
      }
    }
  } catch (...) {
  }
  if (!key.empty() && key.rfind("OMAMENU01", 0) == 0) {
    const auto placement = assets_.Sprite(key);
    if (placement.valid) {
      texture.x = placement.x;
      texture.y = placement.y;
    }
  }
  auto &cached = textures_[key];
  cached = std::move(texture);
  return cached.tex.get();
}

void PmailExtrasMenu::render(Kotonoha::RendererRef renderer) {
  if (auto *background = loadTexture(renderer, "OMAMENU00", "OMAMENU00.CRGB")) {
    const SDL_FRect stage{0, 0, float(kStageWidth), float(kStageHeight)};
    Kotonoha::RenderUpscaledTexture(&renderer, background, nullptr, &stage);
  }

  const int selection = game_.extrasSelection();
  if (selection < 1 || selection > 4)
    return;
  const std::string key = kItemGraphics[size_t(selection - 1)];
  SDL_Texture *item = loadTexture(renderer, key, key + ".CHAR");
  if (!item)
    return;
  const auto &texture = textures_[key];
  const SDL_FRect dst{texture.x, texture.y, texture.w, texture.h};
  Kotonoha::RenderUpscaledTexture(&renderer, item, nullptr, &dst);
}

} // namespace Kotonoha::PmailPort
