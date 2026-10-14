#pragma once
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_HAS_ANIME4K)

bool KotonohaAnime4KInit(SDL_Renderer *renderer);
void KotonohaAnime4KShutdown(void);
bool KotonohaAnime4KUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture);

#else

static inline bool KotonohaAnime4KInit(SDL_Renderer *renderer) {
  (void)renderer;
  return false;
}
static inline void KotonohaAnime4KShutdown(void) {}
static inline bool KotonohaAnime4KUpscaleSDLTextureInto(SDL_Renderer *r,
                                                        SDL_Texture *i,
                                                        SDL_Texture *o) {
  (void)r;
  (void)i;
  (void)o;
  return false;
}

#endif

#ifdef __cplusplus
}
#endif
