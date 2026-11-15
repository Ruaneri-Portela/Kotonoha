#include <Kotonoha/core/PresentedFrame.hpp>

#include <algorithm>
#include <cmath>

namespace Kotonoha {

bool PresentedFrame::capture(RendererRef renderer) {
  SDL_Texture *activeTarget = SDL_GetRenderTarget(&renderer);
  SurfacePtr backbuffer(SDL_RenderReadPixels(&renderer, nullptr));
  if (!backbuffer)
    return false;

  SDL_FRect presentation{0.0f, 0.0f, static_cast<float>(backbuffer->w),
                         static_cast<float>(backbuffer->h)};
  if (!activeTarget) {
    // Backbuffer capture includes native letterboxing, so retain only the Novel
    // presentation. A reduced Novel render target already contains exactly the
    // authored composition and must be retained in full.
    if (!SDL_GetRenderLogicalPresentationRect(&renderer, &presentation))
      return false;
  }
  const int x = std::clamp(static_cast<int>(std::floor(presentation.x)), 0,
                           backbuffer->w);
  const int y = std::clamp(static_cast<int>(std::floor(presentation.y)), 0,
                           backbuffer->h);
  const int right = std::clamp(
      static_cast<int>(std::ceil(presentation.x + presentation.w)), x,
      backbuffer->w);
  const int bottom = std::clamp(
      static_cast<int>(std::ceil(presentation.y + presentation.h)), y,
      backbuffer->h);
  const int width = right - x;
  const int height = bottom - y;
  if (width <= 0 || height <= 0)
    return false;

  SurfacePtr cropped(SDL_CreateSurface(width, height, backbuffer->format));
  if (!cropped)
    return false;
  const SDL_Rect source{x, y, width, height};
  if (!SDL_BlitSurface(backbuffer.get(), &source, cropped.get(), nullptr))
    return false;

  TexturePtr next(SDL_CreateTextureFromSurface(&renderer, cropped.get()));
  if (!next)
    return false;
  if (!SDL_SetTextureBlendMode(next.get(), SDL_BLENDMODE_NONE) ||
      !SDL_SetTextureScaleMode(next.get(), SDL_SCALEMODE_LINEAR))
    return false;
  texture = std::move(next);
  return true;
}

bool PresentedFrame::present(RendererRef renderer) const {
  return texture && SDL_RenderTexture(&renderer, texture.get(), nullptr, nullptr);
}

void PresentedFrame::reset() { texture.reset(); }

} // namespace Kotonoha
