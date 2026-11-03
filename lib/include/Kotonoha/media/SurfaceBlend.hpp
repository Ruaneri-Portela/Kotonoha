#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <algorithm>
#include <stdexcept>

namespace Kotonoha {
// Legacy RGB copies ignore the image mask, but still blend global opacity.
inline void BlitSurfaceOpacity(SurfaceRef source, SurfaceRef destination,
                               const SDL_Rect *sourceRect = nullptr,
                               SDL_Rect *destinationRect = nullptr,
                               int opacity = 255, bool useMask = true) {
  opacity = std::clamp(opacity, 0, 255);
  if (!opacity)
    return;
  SurfacePtr converted;
  SurfacePtr opaqueView;
  auto *input = &source;
  if (&source == &destination) {
    converted.reset(SDL_DuplicateSurface(input));
    if (!converted)
      throw std::runtime_error(SDL_GetError());
    input = converted.get();
  }
  if (!useMask && opacity < 255) {
    if (input->format != SDL_PIXELFORMAT_RGBA32 &&
        input->format != SDL_PIXELFORMAT_RGBX32) {
      converted.reset(SDL_ConvertSurface(input, SDL_PIXELFORMAT_RGBA32));
      if (!converted)
        throw std::runtime_error(SDL_GetError());
      input = converted.get();
    }
    opaqueView.reset(SDL_CreateSurfaceFrom(input->w, input->h,
                                           SDL_PIXELFORMAT_RGBX32,
                                           input->pixels, input->pitch));
    if (!opaqueView)
      throw std::runtime_error(SDL_GetError());
    input = opaqueView.get();
  }
  struct Restore {
    SDL_Surface *surface;
    SDL_BlendMode blend;
    Uint8 alpha;
    ~Restore() {
      SDL_SetSurfaceBlendMode(surface, blend);
      SDL_SetSurfaceAlphaMod(surface, alpha);
    }
  } restore{input, SDL_BLENDMODE_NONE, 255};
  SDL_GetSurfaceBlendMode(input, &restore.blend);
  SDL_GetSurfaceAlphaMod(input, &restore.alpha);
  SDL_SetSurfaceBlendMode(input, useMask || opacity < 255 ? SDL_BLENDMODE_BLEND
                                                          : SDL_BLENDMODE_NONE);
  SDL_SetSurfaceAlphaMod(input, Uint8(opacity));
  if (!SDL_BlitSurface(input, sourceRect, &destination, destinationRect))
    throw std::runtime_error(SDL_GetError());
}
} // namespace Kotonoha
