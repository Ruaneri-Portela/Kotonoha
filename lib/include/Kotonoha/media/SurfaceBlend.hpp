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
    opaqueView.reset(SDL_ConvertSurface(input, SDL_PIXELFORMAT_RGBA32));
    if (!opaqueView)
      throw std::runtime_error(SDL_GetError());
    // A legacy RGB copy ignores the source mask. Make the temporary pixels
    // opaque before applying the requested global alpha through SDL.
    const auto *format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
    for (int y = 0; y < opaqueView->h; ++y) {
      auto *row = reinterpret_cast<Uint32 *>(
          static_cast<Uint8 *>(opaqueView->pixels) + y * opaqueView->pitch);
      for (int x = 0; x < opaqueView->w; ++x)
        row[x] |= format->Amask;
    }
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
