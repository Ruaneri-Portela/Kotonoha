#pragma once

#include <Kotonoha/speciais/Upscaling.h>
#include <SDL3/SDL_render.h>

namespace Kotonoha {

class Upscaling {
protected:
  SDL_Texture *extraTexture = nullptr;
  SDL_Texture *upScaledTexture = nullptr;
  int baseWidth = 0;
  int baseHeight = 0;
  float currentFactor = 1.0f;

  bool EnsureUpscalingTextures(SDL_Renderer *render, int width, int height,
                               float factor);

public:
  Upscaling() = default;
  Upscaling(int baseWidth, int baseHeight);
  virtual ~Upscaling();

  bool IsUpscalingActive() const;

  // Prepares the intermediate texture and upscaled output texture for rendering
  bool PrepareUpscale(SDL_Renderer *render, int width, int height);

  // Executes the upscaling pass from extraTexture into upScaledTexture
  bool ProcessUpscale(SDL_Renderer *render, SDL_Texture *srcTexutre);

  void ResetUpscaling();
};

} // namespace Kotonoha
