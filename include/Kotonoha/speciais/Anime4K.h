#pragma once
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_HAS_ANIME4K)

bool Kotonoha_Anime4KInit(SDL_Renderer *renderer);
void Kotonoha_Anime4KShutdown(void);
bool Kotonoha_Anime4KUpscaleSDLTextureInto(
    SDL_Renderer *renderer,
    SDL_Texture *inputTexture,
    SDL_Texture *outputTexture);

#else

static inline bool Kotonoha_Anime4KInit(SDL_Renderer *renderer) { (void)renderer; return false; }
static inline void Kotonoha_Anime4KShutdown(void) {}
static inline bool Kotonoha_Anime4KUpscaleSDLTextureInto(SDL_Renderer *r, SDL_Texture *i, SDL_Texture *o) { (void)r; (void)i; (void)o; return false; }

#endif

#ifdef __cplusplus
}
#endif
