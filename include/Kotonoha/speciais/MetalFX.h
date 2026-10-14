#pragma once
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_HAS_METALFX)

bool KotonohaMetalFXInit(SDL_Renderer *renderer);
void KotonohaMetalFXShutdown(void);
bool KotonohaMetalFXUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture);
void KotonohaMetalFXClearScalerCache(void);

#else

static inline bool KotonohaMetalFXInit(SDL_Renderer *renderer) {
  (void)renderer;
  return false;
}
static inline void KotonohaMetalFXShutdown(void) {}
static inline bool KotonohaMetalFXUpscaleSDLTextureInto(SDL_Renderer *r,
                                                        SDL_Texture *i,
                                                        SDL_Texture *o) {
  (void)r;
  (void)i;
  (void)o;
  return false;
}
static inline void KotonohaMetalFXClearScalerCache(void) {}

#endif

#ifdef __cplusplus
}
#endif
