#pragma once
#include <SDL3/SDL.h>

#include <Kotonoha/parsers/Config.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_HAS_ANIME4K)

// Lista separada por ";" de arquivos em assets/Anime4K (NULL/vazio = padrão)
void KotonohaAnime4KSetFiles(const char *files);
// Usa config->anime4kFiles como lista de shaders
bool KotonohaAnime4KInit(SDL_Renderer *renderer, Kotonoha_Config *config);
void KotonohaAnime4KShutdown(void);
bool KotonohaAnime4KUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture);

#else

static inline void KotonohaAnime4KSetFiles(const char *files) { (void)files; }
static inline bool KotonohaAnime4KInit(SDL_Renderer *renderer,
                                       Kotonoha_Config *config) {
  (void)renderer;
  (void)config;
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
