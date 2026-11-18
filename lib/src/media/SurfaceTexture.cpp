#include <Kotonoha/media/SurfaceTexture.hpp>
#include <Kotonoha/media/TextureAlpha.hpp>

namespace Kotonoha {

SurfacePtr prepareSurfaceForLinearSampling(SDL_Surface *source) {
  if (!source || source->w <= 0 || source->h <= 0)
    return {};

  SurfacePtr rgba;
  if (SDL_SurfaceHasColorKey(source)) {
    // SDL_ConvertSurface may carry color-key metadata into the destination.
    // Instead, blit onto transparent RGBA pixels: SDL respects the original
    // source pixel format and palette index when evaluating its color key.
    rgba.reset(SDL_CreateSurface(source->w, source->h, SDL_PIXELFORMAT_RGBA32));
    if (!rgba ||
        !SDL_FillSurfaceRect(rgba.get(), nullptr,
                             SDL_MapSurfaceRGBA(rgba.get(), 0, 0, 0, 0)))
      return {};
    SDL_BlendMode previous = SDL_BLENDMODE_NONE;
    if (!SDL_GetSurfaceBlendMode(source, &previous) ||
        !SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE))
      return {};
    const bool copied = SDL_BlitSurface(source, nullptr, rgba.get(), nullptr);
    const bool restored = SDL_SetSurfaceBlendMode(source, previous);
    if (!copied || !restored)
      return {};
  } else {
    rgba.reset(SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32));
    if (!rgba)
      return {};
  }

  const bool lock = SDL_MUSTLOCK(rgba.get());
  if (lock && !SDL_LockSurface(rgba.get()))
    return {};
  prepareStraightAlphaForLinearFiltering(static_cast<Uint8 *>(rgba->pixels),
                                         rgba->w, rgba->h, rgba->pitch);
  if (lock)
    SDL_UnlockSurface(rgba.get());
  return rgba;
}

} // namespace Kotonoha
