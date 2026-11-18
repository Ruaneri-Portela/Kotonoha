#pragma once

#include <SDL3/SDL.h>

namespace RealLive {

// A completed RealLive DC composition represents a full scene and must
// replace, not alpha-blend with, the engine's framebuffer. Otherwise a
// partially/fully transparent DC can appear black while dialogue remains
// visible (it is rendered in a subsequent pass).
inline bool configureSceneTexture(SDL_Texture *texture) {
  return texture &&
         SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) &&
         SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
}

} // namespace RealLive
