#pragma once
#include <SDL3/SDL.h>

#include <Kotonoha/parsers/Config.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_HAS_MPV_UPSCALER)

// Lista separada por ";" de arquivos em assets/Anime4K (NULL/vazio = padrão)
void KotonohaMPVUpscalerSetFiles(const char *files);
// Usa config->mpvUpscalerFiles como lista de shaders
bool KotonohaMPVUpscalerInit(SDL_Renderer *renderer, Kotonoha_Config *config);
void KotonohaMPVUpscalerShutdown(void);
bool KotonohaMPVUpscalerUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture);

#else

static inline void KotonohaMPVUpscalerSetFiles(const char *files) { (void)files; }
static inline bool KotonohaMPVUpscalerInit(SDL_Renderer *renderer,
                                       Kotonoha_Config *config) {
  (void)renderer;
  (void)config;
  return false;
}
static inline void KotonohaMPVUpscalerShutdown(void) {}
static inline bool KotonohaMPVUpscalerUpscaleSDLTextureInto(SDL_Renderer *r,
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
