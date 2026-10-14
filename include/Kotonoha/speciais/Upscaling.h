#pragma once
#include <SDL3/SDL.h>

#include <Kotonoha/speciais/Anime4K.h>
#include <Kotonoha/speciais/MetalFX.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Kotonoha_UpscalerType {
  KOTONOHA_UPSCALER_NONE = 0,
  KOTONOHA_UPSCALER_ANIME4K = 1,
  KOTONOHA_UPSCALER_METALFX = 2
} Kotonoha_UpscalerType;

// Generic Init & Shutdown
bool KotonohaUpscalingInitInit(SDL_Renderer *renderer, const char *name,
                               float factor);
void KotonohaUpscalingInitShutdown(void);

// Capabilities & Selection
bool Kotonoha_HasUpscaler(void);
bool Kotonoha_HasMetalFX(void);
bool Kotonoha_HasAnime4K(void);

Kotonoha_UpscalerType Kotonoha_GetUpscalerType(void);
const char *Kotonoha_GetUpscalerName(void);
void Kotonoha_SetUpscalerType(Kotonoha_UpscalerType type);
bool Kotonoha_SetUpscalerByName(const char *name);

// Upscale Factor (e.g. 1.0f = disabled, 1.5f, 2.0f)
float KotonohaUpscalingInitFactor(void);
void KotonohaUpscalingInitSetFactor(float factor);

// Generic Upscale execution function (routes to MetalFX or Anime4K/OpenGL)
bool Kotonoha_UpscaleSDLTextureInto(SDL_Renderer *renderer,
                                    SDL_Texture *inputTexture,
                                    SDL_Texture *outputTexture);

#ifdef __cplusplus
}
#endif
