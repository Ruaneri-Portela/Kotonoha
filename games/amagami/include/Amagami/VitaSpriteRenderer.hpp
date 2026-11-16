#pragma once

#include <Amagami/VitaVmBridge.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <functional>
#include <map>
#include <utility>

namespace Amagami::Vita {

// Renderer for the current subset of Vita Sprite objects only. Script-space
// origin is the middle of the 480x272 view; on-screen size is 960x544.
// Sprites use the UI view: K2X_Script world scale/origin are deliberately not
// applied to them (the original engine keeps Sprite in the fixed UI view).
// Does not call SDL_RenderPresent: embed in a Kotonoha draw pass or use the
// read-only VitaProbe --snapshot diagnostic.
class VitaSpriteRenderer {
public:
  using SurfaceFactory = std::function<
      Kotonoha::SurfacePtr(uint32_t textureId, uint32_t variant)>;

  explicit VitaSpriteRenderer(SDL_Renderer *renderer, SurfaceFactory source);
  void draw(const VitaVmBridge &bridge, bool clear = true);
  void clearCache() { cache_.clear(); }
  std::size_t cachedTextures() const { return cache_.size(); }

private:
  SDL_Texture *textureFor(uint32_t id, uint32_t variant);
  SDL_Renderer *renderer_;
  SurfaceFactory source_;
  std::map<std::pair<uint32_t, uint32_t>, Kotonoha::TexturePtr> cache_;
};

} // namespace Amagami::Vita
