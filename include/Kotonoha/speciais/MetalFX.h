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

#else

static inline bool Kotonoha_MetalFXInit(SDL_Renderer *renderer) { (void)renderer; return false; }
static inline void Kotonoha_MetalFXShutdown(void) {}
static inline bool Kotonoha_MetalFXUpscaleSDLTextureInto(SDL_Renderer *r, SDL_Texture *i, SDL_Texture *o) { (void)r; (void)i; (void)o; return false; }
static inline void Kotonoha_MetalFXClearScalerCache(void) {}

#endif

#ifdef __cplusplus
}
#endif
