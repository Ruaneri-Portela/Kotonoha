#include <Kotonoha/components/Upscaling.hpp>
#include <Kotonoha/utils/Log.h>
#include <cmath>

namespace Kotonoha {

Upscaling::Upscaling(int baseWidth, int baseHeight)
    : baseWidth(baseWidth), baseHeight(baseHeight) {}

Upscaling::~Upscaling() { ResetUpscaling(); }

bool Upscaling::IsUpscalingActive() const {
  const float factor = KotonohaUpscalingGetFactor();
  return KotonohaUpscalingIsEnabled() && factor > 1.0f &&
         Kotonoha_GetUpscalerType() != KOTONOHA_UPSCALER_NONE;
}

bool Upscaling::IsUpscaleCached(Uint64 key) const {
  return cacheValid && cachedKey == key && upScaledTexture != nullptr;
}

void Upscaling::MarkUpscaleCached(Uint64 key) {
  cachedKey = key;
  cacheValid = true;
}

Uint64 Upscaling::HashCombine(Uint64 seed, Uint64 value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

void Upscaling::ResetUpscaling() {
  cacheValid = false;
  if (extraTexture != nullptr) {
    SDL_DestroyTexture(extraTexture);
    extraTexture = nullptr;
  }
  if (upScaledTexture != nullptr) {
    SDL_DestroyTexture(upScaledTexture);
    upScaledTexture = nullptr;
  }
}

bool Upscaling::EnsureUpscalingTextures(SDL_Renderer *render, int width,
                                        int height, float factor) {
  if (render == nullptr || width <= 0 || height <= 0 || factor <= 1.0f) {
    return false;
  }

  const int targetUpscaledW = static_cast<int>(std::lround(width * factor));
  const int targetUpscaledH = static_cast<int>(std::lround(height * factor));

  const bool needsNewTextures =
      (extraTexture == nullptr) || (upScaledTexture == nullptr) ||
      (baseWidth != width) || (baseHeight != height) ||
      (currentFactor != factor);

  if (!needsNewTextures) {
    return true;
  }

  ResetUpscaling();

  baseWidth = width;
  baseHeight = height;
  currentFactor = factor;

  extraTexture = SDL_CreateTexture(render, SDL_PIXELFORMAT_BGRA32,
                                   SDL_TEXTUREACCESS_TARGET, width, height);

  if (extraTexture == nullptr) {
    KLOG_ERROR(UPSCALER,
               "Failed to create intermediate upscaling texture (%dx%d): %s",
               width, height, SDL_GetError());
    return false;
  }

  SDL_SetTextureScaleMode(extraTexture, SDL_SCALEMODE_LINEAR);
  SDL_SetTextureBlendMode(extraTexture, SDL_BLENDMODE_BLEND);

  upScaledTexture = SDL_CreateTexture(render, SDL_PIXELFORMAT_BGRA32,
                                      SDL_TEXTUREACCESS_TARGET, targetUpscaledW,
                                      targetUpscaledH);

  if (upScaledTexture == nullptr) {
    KLOG_ERROR(UPSCALER, "Failed to create output upscaled texture (%dx%d): %s",
               targetUpscaledW, targetUpscaledH, SDL_GetError());
    ResetUpscaling();
    return false;
  }

  SDL_SetTextureScaleMode(upScaledTexture, SDL_SCALEMODE_LINEAR);
  SDL_SetTextureBlendMode(upScaledTexture, SDL_BLENDMODE_BLEND);

  return true;
}

bool Upscaling::ProcessUpscale(SDL_Renderer *render, SDL_Texture *srcTexutre) {
  if (render == nullptr || srcTexutre == nullptr ||
      upScaledTexture == nullptr) {
    return false;
  }
  // Texturas YUV/NV12 expõem só o plano Y ao OpenGL (resultado P&B); o SDL as
  // converte para RGB ao desenhar na textura intermediária.
  if (srcTexutre != extraTexture && extraTexture != nullptr) {
    const SDL_PixelFormat format = srcTexutre->format;
    if (format != SDL_PIXELFORMAT_RGBA32 && format != SDL_PIXELFORMAT_BGRA32 &&
        format != SDL_PIXELFORMAT_ARGB8888 &&
        format != SDL_PIXELFORMAT_XRGB8888) {
      SDL_Texture *previousTarget = SDL_GetRenderTarget(render);
      if (!SDL_SetRenderTarget(render, extraTexture)) {
        return false;
      }
      SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
      SDL_RenderClear(render);
      const bool copied =
          SDL_RenderTexture(render, srcTexutre, nullptr, nullptr);
      SDL_SetRenderTarget(render, previousTarget);
      if (!copied) {
        return false;
      }
      srcTexutre = extraTexture;
    }
  }
  return Kotonoha_UpscaleSDLTextureInto(render, srcTexutre, upScaledTexture);
}
} // namespace Kotonoha
