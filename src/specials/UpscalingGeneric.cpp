#include <SDL3/SDL.h>
#include <Kotonoha/speciais/Upscaling.h>

static float g_upscalingFactor = 1.0f;

namespace {


#if defined(KOTONOHA_HAS_METALFX)
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_METALFX;
#elif defined(KOTONOHA_HAS_ANIME4K)
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
#else
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_NONE;
#endif

} // namespace

extern "C" {

bool Kotonoha_UpscalingInit(SDL_Renderer *renderer, const char *name, float factor) {
    g_upscalingFactor = factor;

    if (factor <= 1.0f) {
        g_upscalerType = KOTONOHA_UPSCALER_NONE;
        return true;
    }

    if (name != NULL && SDL_strcasecmp(name, "Auto") != 0) {
        if (!Kotonoha_SetUpscalerByName(name)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Requested upscaler '%s' is not supported.", name);
            g_upscalerType = KOTONOHA_UPSCALER_NONE;
            g_upscalingFactor = 1.0f;
            return false;
        }
    } else {
        const char *rendererName = renderer != NULL ? SDL_GetRendererName(renderer) : NULL;
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
            if (Kotonoha_HasMetalFX()) {
                g_upscalerType = KOTONOHA_UPSCALER_METALFX;
            } else if (Kotonoha_HasAnime4K()) {
                g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
            } else {
                g_upscalerType = KOTONOHA_UPSCALER_NONE;
            }
        }
    }

    if (g_upscalerType == KOTONOHA_UPSCALER_METALFX) {
#if defined(KOTONOHA_HAS_METALFX)
        if (Kotonoha_MetalFXInit(renderer)) {
            SDL_Log("Upscaling initialized with MetalFX (factor: %.2f)", factor);
            return true;
        }
#endif
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "MetalFX initialization failed; falling back to Anime4K.");
        g_upscalerType = KOTONOHA_UPSCALER_ANIME4K;
    }

    if (g_upscalerType == KOTONOHA_UPSCALER_ANIME4K) {
#if defined(KOTONOHA_HAS_ANIME4K)
        if (Kotonoha_Anime4KInit(renderer)) {
            SDL_Log("Upscaling initialized with Anime4K / OpenGL (factor: %.2f)", factor);
            return true;
        }
#endif
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Anime4K initialization failed.");
    }

    // Failure: disable upscaling
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Disabling upscaler due to initialization failure.");
    g_upscalerType = KOTONOHA_UPSCALER_NONE;
    g_upscalingFactor = 1.0f;
    return false;
}

void Kotonoha_UpscalingShutdown(void) {
#if defined(KOTONOHA_HAS_METALFX)
    Kotonoha_MetalFXShutdown();
#endif
#if defined(KOTONOHA_HAS_ANIME4K)
    Kotonoha_Anime4KShutdown();
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

Kotonoha_UpscalerType Kotonoha_GetUpscalerType(void) {
    return g_upscalerType;
}

const char* Kotonoha_GetUpscalerName(void) {
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

bool Kotonoha_SetUpscalerByName(const char* name) {
    if (name == NULL) return false;

    if (SDL_strcasecmp(name, "MetalFX") == 0) {
        if (Kotonoha_HasMetalFX()) {
            g_upscalerType = KOTONOHA_UPSCALER_METALFX;
            return true;
        }
        return false;
    }
    if (SDL_strcasecmp(name, "Anime4K") == 0 || SDL_strcasecmp(name, "OpenGL") == 0 || SDL_strcasecmp(name, "GLES") == 0) {
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

float Kotonoha_UpscalingFactor(void) {
    return g_upscalingFactor;
}

void Kotonoha_UpscalingSetFactor(float factor) {
    g_upscalingFactor = factor;
}

bool Kotonoha_UpscaleSDLTextureInto(
    SDL_Renderer *renderer,
    SDL_Texture *inputTexture,
    SDL_Texture *outputTexture)
{
    if (g_upscalerType == KOTONOHA_UPSCALER_METALFX) {
#if defined(KOTONOHA_HAS_METALFX)
        if (Kotonoha_MetalFXUpscaleSDLTextureInto(renderer, inputTexture, outputTexture)) {
            return true;
        }
#endif
    } else if (g_upscalerType == KOTONOHA_UPSCALER_ANIME4K) {
#if defined(KOTONOHA_HAS_ANIME4K)
        if (Kotonoha_Anime4KUpscaleSDLTextureInto(renderer, inputTexture, outputTexture)) {
            return true;
        }
#endif
    }

    // Upscale pass failed or NONE selected: disable upscaler and fallback to standard render
    g_upscalerType = KOTONOHA_UPSCALER_NONE;
    g_upscalingFactor = 1.0f;

    SDL_SetRenderTarget(renderer, outputTexture);
    SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
    SDL_SetRenderTarget(renderer, NULL);
    return true;
}

} // extern "C"
