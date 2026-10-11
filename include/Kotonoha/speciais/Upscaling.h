#pragma once
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_HAS_METALFX)

bool Kotonoha_MetalFXInit(SDL_Renderer *renderer);

void Kotonoha_MetalFXShutdown(void);

bool Kotonoha_MetalFXUpscaleSDLTextureInto(
    SDL_Renderer *renderer,
    SDL_Texture *inputTexture,
    SDL_Texture *outputTexture);

void Kotonoha_MetalFXClearScalerCache(void);

float Kotonoha_UpscalingFactor(void);

void Kotonoha_UpscalingSetFactor(float factor);

#else

static inline bool Kotonoha_MetalFXInit(SDL_Renderer *renderer) {
    (void)renderer;
    return false;
}

static inline void Kotonoha_MetalFXShutdown(void) {}

static inline bool Kotonoha_MetalFXUpscaleSDLTextureInto(
    SDL_Renderer *renderer,
    SDL_Texture *inputTexture,
    SDL_Texture *outputTexture)
{
    (void)renderer;
    (void)inputTexture;
    (void)outputTexture;
    return false;
}

static inline void Kotonoha_MetalFXClearScalerCache(void) {}

static inline float Kotonoha_UpscalingFactor(void) {
    return 1.0f;
}

static inline void Kotonoha_UpscalingSetFactor(float factor) {
    (void)factor;
}

#endif

#ifdef __cplusplus
}
#endif
