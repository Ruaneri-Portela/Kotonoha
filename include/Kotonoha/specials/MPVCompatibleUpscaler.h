#pragma once
#include <SDL3/SDL.h>

#include <Kotonoha/parsers/Config.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KOTONOHA_HAS_MPV_UPSCALER)

// Lista separada por ":" de arquivos no diretório do preset ativo
// (NULL/vazio = cadeia padrão do preset).
void KotonohaMPVUpscalerSetFiles(const char *files);
// Generic upscaler preset value (Kotonoha_UpscalerPreset). Kept as int here so
// this low-level backend header does not depend on Upscaling.h and create an
// include cycle.
void KotonohaMPVUpscalerSetPreset(int preset);
// Usa config->mpvUpscalerFiles como lista de shaders
bool KotonohaMPVUpscalerInit(SDL_Renderer *renderer, Kotonoha_Config *config);
void KotonohaMPVUpscalerShutdown(void);
// Non-zero only while a transient backend failure is waiting for its bounded
// retry window.  Generic texture caches use this to invalidate exactly when a
// retry becomes due instead of pinning the temporary SDL fallback forever.
Uint64 KotonohaMPVUpscalerRetryDeadlineMs(void);
bool KotonohaMPVUpscalerUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture);

#else

static inline void KotonohaMPVUpscalerSetFiles(const char *files) { (void)files; }
static inline void KotonohaMPVUpscalerSetPreset(int preset) { (void)preset; }
static inline bool KotonohaMPVUpscalerInit(SDL_Renderer *renderer,
                                       Kotonoha_Config *config) {
  (void)renderer;
  (void)config;
  return false;
}
static inline void KotonohaMPVUpscalerShutdown(void) {}
static inline Uint64 KotonohaMPVUpscalerRetryDeadlineMs(void) { return 0; }
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
