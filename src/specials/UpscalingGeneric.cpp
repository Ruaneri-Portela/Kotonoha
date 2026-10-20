#include <Kotonoha/specials/Upscaling.h>
#include <Kotonoha/utils/Log.h>
#include <SDL3/SDL.h>

static float g_upscalingFactor = 1.0f;
static bool g_upscalingEnabled = true;

namespace {

#if defined(KOTONOHA_HAS_METALFX)
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_METALFX;
#elif defined(KOTONOHA_HAS_ANIME4K)
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
#else
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_NONE;
#endif

static bool CopyTextureToTarget(SDL_Renderer *renderer,
                                SDL_Texture *inputTexture,
                                SDL_Texture *outputTexture) {
  SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
  if (!SDL_SetRenderTarget(renderer, outputTexture)) {
    KLOG_ERROR(UPSCALER, "Failed to set upscale fallback target: %s",
               SDL_GetError());
    return false;
  }

  const bool copied = SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
  const bool restored = SDL_SetRenderTarget(renderer, previousTarget);
  if (!copied) {
    KLOG_ERROR(UPSCALER,
               "Failed to copy source texture in upscale fallback: %s",
               SDL_GetError());
  }
  if (!restored) {
    KLOG_ERROR(UPSCALER,
               "Failed to restore render target after upscale fallback: %s",
               SDL_GetError());
  }
  return copied && restored;
}

} // namespace

extern "C" {

bool KotonohaUpscalingIsEnabled(void) { return g_upscalingEnabled; }
void KotonohaUpscalingSetEnabled(bool enabled) { g_upscalingEnabled = enabled; }

bool KotonohaUpscalingInit(SDL_Renderer *renderer, const char *name,
                           Kotonoha_Config *config) {
  if (config == NULL) {
    return false;
  }
  g_upscalingEnabled = config->upscalingEnabled;
  g_upscalingFactor = config->scalerFactor;

  if (config->scalerFactor <= 1.0f) {
    KLOG_INFO(UPSCALER, "Upscaling disabled (factor %.2f)",
              config->scalerFactor.load());
    g_upscalerType = KOTONOHA_UPSCALER_NONE;
    return true;
  }

  if (name != NULL && SDL_strcasecmp(name, "Auto") != 0) {
    if (!Kotonoha_SetUpscalerByName(name)) {
      KLOG_WARN(UPSCALER, "Requested upscaler '%s' is not supported.", name);
      g_upscalerType = KOTONOHA_UPSCALER_NONE;
      g_upscalingFactor = 1.0f;
      return false;
    }
  } else {
    const char *rendererName =
        renderer != NULL ? SDL_GetRendererName(renderer) : NULL;
    bool isMetal = false;
    bool isOpenGL = false;

    if (rendererName != NULL) {
      if (SDL_strcasestr(rendererName, "metal") != NULL) {
        isMetal = true;
      } else if (SDL_strcasestr(rendererName, "opengl") != NULL ||
                 SDL_strcasestr(rendererName, "opengles") != NULL ||
                 SDL_strcasestr(rendererName, "gles") != NULL) {
        isOpenGL = true;
      }
    }

    if (isMetal && Kotonoha_HasMetalFX()) {
      g_upscalerType = KOTONOHA_UPSCALER_METALFX;
    } else if (isOpenGL && Kotonoha_HasAnime4K()) {
      g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
    } else {
      g_upscalerType = KOTONOHA_UPSCALER_NONE;
    }
  }

  if (g_upscalerType == KOTONOHA_UPSCALER_METALFX) {
#if defined(KOTONOHA_HAS_METALFX)
    if (KotonohaMetalFXInit(renderer, config)) {
      KLOG_INFO(UPSCALER, "Upscaling initialized with MetalFX (factor: %.2f)",
                g_upscalingFactor);
      return true;
    }
#endif
    KLOG_WARN(UPSCALER,
              "MetalFX initialization failed; falling back to Anime4K.");
    g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
  }

  if (g_upscalerType == KOTONOHA_UPSCALER_ANIME4K) {
#if defined(KOTONOHA_HAS_ANIME4K)
    if (KotonohaAnime4KInit(renderer, config)) {
      KLOG_INFO(UPSCALER,
                "Upscaling initialized with Anime4K / OpenGL (factor: %.2f)",
                g_upscalingFactor);
      return true;
    }
#endif
    KLOG_WARN(UPSCALER, "Anime4K initialization failed.");
  }

  if (g_upscalerType == KOTONOHA_UPSCALER_ANIME4K) {
    KLOG_WARN(UPSCALER, "Upscaler initialization failed.");
  }

  // Failure: disable upscaling
  KLOG_WARN(UPSCALER, "Disabling upscaler due to initialization failure.");
  g_upscalerType = KOTONOHA_UPSCALER_NONE;
  g_upscalingFactor = 1.0f;
  return false;
}

void KotonohaUpscalingInitShutdown(void) {
#if defined(KOTONOHA_HAS_METALFX)
  KotonohaMetalFXShutdown();
#endif
#if defined(KOTONOHA_HAS_ANIME4K)
  KotonohaAnime4KShutdown();
#endif
  g_upscalerType = KOTONOHA_UPSCALER_NONE;
}

bool Kotonoha_HasUpscaler(void) {
  return Kotonoha_HasMetalFX() || Kotonoha_HasAnime4K();
}

bool Kotonoha_HasMetalFX(void) {
#if defined(KOTONOHA_HAS_METALFX)
  return true;
#else
  return false;
#endif
}

bool Kotonoha_HasAnime4K(void) {
#if defined(KOTONOHA_HAS_ANIME4K)
  return true;
#else
  return false;
#endif
}

Kotonoha_UpscalerType Kotonoha_GetUpscalerType(void) { return g_upscalerType; }

const char *Kotonoha_GetUpscalerName(void) {
  switch (g_upscalerType) {
  case KOTONOHA_UPSCALER_METALFX:
    return "MetalFX";
  case KOTONOHA_UPSCALER_ANIME4K:
    return "Anime4K";
  default:
    return "None";
  }
}

void Kotonoha_SetUpscalerType(Kotonoha_UpscalerType type) {
  g_upscalerType = type;
}

bool Kotonoha_SetUpscalerByName(const char *name) {
  if (name == NULL)
    return false;

  if (SDL_strcasecmp(name, "MetalFX") == 0) {
    if (Kotonoha_HasMetalFX()) {
      g_upscalerType = KOTONOHA_UPSCALER_METALFX;
      return true;
    }
    return false;
  }
  if (SDL_strcasecmp(name, "Anime4K") == 0 ||
      SDL_strcasecmp(name, "OpenGL") == 0 ||
      SDL_strcasecmp(name, "GLES") == 0) {
    if (Kotonoha_HasAnime4K()) {
      g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
      return true;
    }
    return false;
  }
  if (SDL_strcasecmp(name, "None") == 0 || SDL_strcasecmp(name, "Off") == 0) {
    g_upscalerType = KOTONOHA_UPSCALER_NONE;
    return true;
  }
  if (SDL_strcasecmp(name, "Auto") == 0) {
    if (Kotonoha_HasMetalFX()) {
      g_upscalerType = KOTONOHA_UPSCALER_METALFX;
    } else if (Kotonoha_HasAnime4K()) {
      g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
    } else {
      g_upscalerType = KOTONOHA_UPSCALER_NONE;
    }
    return true;
  }
  return false;
}

float KotonohaUpscalingGetFactor(void) { return g_upscalingFactor; }

void KotonohaUpscalingSetFactor(float factor) { g_upscalingFactor = factor; }

bool Kotonoha_UpscaleSDLTextureInto(SDL_Renderer *renderer,
                                    SDL_Texture *inputTexture,
                                    SDL_Texture *outputTexture) {
  if (g_upscalerType == KOTONOHA_UPSCALER_METALFX) {
#if defined(KOTONOHA_HAS_METALFX)
    if (KotonohaMetalFXUpscaleSDLTextureInto(renderer, inputTexture,
                                             outputTexture)) {
      return true;
    }
#endif
  } else if (g_upscalerType == KOTONOHA_UPSCALER_ANIME4K) {
#if defined(KOTONOHA_HAS_ANIME4K)
    if (KotonohaAnime4KUpscaleSDLTextureInto(renderer, inputTexture,
                                             outputTexture)) {
      return true;
    }
#endif
  }

  // Upscale pass failed or NONE selected: disable upscaler and fallback to
  // standard render
  g_upscalerType = KOTONOHA_UPSCALER_NONE;
  g_upscalingFactor = 1.0f;

  return CopyTextureToTarget(renderer, inputTexture, outputTexture);
}

} // extern "C"
