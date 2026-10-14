#include <Kotonoha/components/Upscaling.hpp>
#include <cmath>

namespace Kotonoha {

Upscaling::Upscaling(int baseWidth, int baseHeight)
    : baseWidth(baseWidth), baseHeight(baseHeight) {}

Upscaling::~Upscaling() { ResetUpscaling(); }

bool Upscaling::IsUpscalingActive() const {
  const float factor = KotonohaUpscalingInitFactor();
  return factor > 1.0f && Kotonoha_HasUpscaler();
}

void Upscaling::ResetUpscaling() {
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
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
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
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to create output upscaled texture (%dx%d): %s",
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
  return Kotonoha_UpscaleSDLTextureInto(render, srcTexutre, upScaledTexture);
}
} // namespace Kotonoha
