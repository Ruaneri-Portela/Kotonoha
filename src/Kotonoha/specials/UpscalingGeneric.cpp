#include <Kotonoha/specials/Upscaling.h>
#include <Kotonoha/utils/Log.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <climits>

namespace Kotonoha::Detail {

static float g_upscalingFactor = 1.0f;
static bool g_upscalingEnabled = true;
static Kotonoha_UpscalerPreset g_upscalingPreset =
    KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K;
static Uint64 g_upscalingRevision = 1;
static Uint64 g_failedUpscalerRevision = 0;
static bool g_loggedUnavailableBackend = false;
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
static Uint64 g_observedMpvRetryDeadlineMs = 0;
#endif

static void AdvanceUpscalingRevision() {
  ++g_upscalingRevision;
  if (g_upscalingRevision == 0) {
    ++g_upscalingRevision;
  }
  g_failedUpscalerRevision = 0;
  g_loggedUnavailableBackend = false;
}

#if defined(KOTONOHA_HAS_METALFX)
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_METALFX;
#elif defined(KOTONOHA_HAS_MPV_UPSCALER)
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_MPV_COMPATIBLE;
#else
static Kotonoha_UpscalerType g_upscalerType = KOTONOHA_UPSCALER_NONE;
#endif

static bool CopyTextureToTarget(SDL_Renderer *renderer,
                                SDL_Texture *inputTexture,
                                SDL_Texture *outputTexture) {
  SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
  SDL_BlendMode previousBlend = SDL_BLENDMODE_BLEND;
  Uint8 previousR = 0;
  Uint8 previousG = 0;
  Uint8 previousB = 0;
  Uint8 previousA = 0;
  const bool blendSaved = SDL_GetTextureBlendMode(inputTexture, &previousBlend);
  const bool colorSaved =
      SDL_GetRenderDrawColor(renderer, &previousR, &previousG, &previousB,
                             &previousA);
  if (!blendSaved || !colorSaved) {
    return false;
  }
  if (!SDL_SetRenderTarget(renderer, outputTexture)) {
    KLOG_ERROR(UPSCALER, "Failed to set upscale fallback target: %s",
               SDL_GetError());
    return false;
  }

  const bool prepared = SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) &&
                        SDL_SetTextureBlendMode(inputTexture,
                                                SDL_BLENDMODE_NONE) &&
                        SDL_RenderClear(renderer);
  const bool copied =
      prepared && SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
  const bool blendRestored =
      SDL_SetTextureBlendMode(inputTexture, previousBlend);
  const bool colorRestored =
      SDL_SetRenderDrawColor(renderer, previousR, previousG, previousB,
                             previousA);
  const bool restored = SDL_SetRenderTarget(renderer, previousTarget);
  if (!prepared || !copied || !blendRestored || !colorRestored || !restored) {
    KLOG_ERROR(UPSCALER,
               "Failed to scale source texture in fallback: %s",
               SDL_GetError());
  }
  return prepared && copied && blendRestored && colorRestored && restored;
}

static bool IsSupportedUpscaleInputFormat(SDL_PixelFormat format) {
  return format == SDL_PIXELFORMAT_RGBA32 || format == SDL_PIXELFORMAT_BGRA32 ||
         format == SDL_PIXELFORMAT_ARGB8888 ||
         format == SDL_PIXELFORMAT_XRGB8888;
}

static void StoreUpscaledTextureMetadata(Kotonoha_UpscaledTexture *cached,
                                         Uint64 sourceSerial,
                                         Uint64 revision, int width,
                                         int height, Uint32 format,
                                         float factor) {
  cached->sourceSerial = sourceSerial;
  cached->settingsRevision = revision;
  cached->sourceWidth = width;
  cached->sourceHeight = height;
  cached->sourceFormat = format;
  cached->factor = factor;
  cached->initialized = true;
}

static bool ConvertTextureForUpscaling(SDL_Renderer *renderer,
                                      SDL_Texture *source,
                                      SDL_Texture *intermediate) {
  SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
  SDL_Rect previousViewport{};
  SDL_Rect previousClip{};
  float previousScaleX = 1.0f;
  float previousScaleY = 1.0f;
  Uint8 previousR = 0;
  Uint8 previousG = 0;
  Uint8 previousB = 0;
  Uint8 previousA = 0;
  SDL_BlendMode previousBlend = SDL_BLENDMODE_BLEND;

  const bool viewportEnabled = SDL_RenderViewportSet(renderer);
  const bool clipEnabled = SDL_RenderClipEnabled(renderer);
  const bool viewportSaved =
      SDL_GetRenderViewport(renderer, &previousViewport);
  const bool clipSaved = SDL_GetRenderClipRect(renderer, &previousClip);
  const bool scaleSaved =
      SDL_GetRenderScale(renderer, &previousScaleX, &previousScaleY);
  const bool colorSaved =
      SDL_GetRenderDrawColor(renderer, &previousR, &previousG, &previousB,
                             &previousA);
  const bool blendSaved = SDL_GetTextureBlendMode(source, &previousBlend);

  if (!viewportSaved || !clipSaved || !scaleSaved || !colorSaved ||
      !blendSaved) {
    return false;
  }
  if (!SDL_SetRenderTarget(renderer, intermediate)) {
    return false;
  }

  const bool prepared =
      SDL_SetRenderViewport(renderer, nullptr) &&
      SDL_SetRenderClipRect(renderer, nullptr) &&
      SDL_SetRenderScale(renderer, 1.0f, 1.0f) &&
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) &&
      SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE) &&
      SDL_RenderClear(renderer);
  const bool copied =
      prepared && SDL_RenderTexture(renderer, source, nullptr, nullptr);

  const bool blendRestored =
      SDL_SetTextureBlendMode(source, previousBlend);
  const bool targetRestored = SDL_SetRenderTarget(renderer, previousTarget);
  const bool viewportRestored = SDL_SetRenderViewport(
      renderer, viewportEnabled ? &previousViewport : nullptr);
  const bool clipRestored = SDL_SetRenderClipRect(
      renderer, clipEnabled ? &previousClip : nullptr);
  const bool scaleRestored =
      SDL_SetRenderScale(renderer, previousScaleX, previousScaleY);
  const bool colorRestored = SDL_SetRenderDrawColor(
      renderer, previousR, previousG, previousB, previousA);

  if (!prepared || !copied || !blendRestored || !targetRestored ||
      !viewportRestored || !clipRestored || !scaleRestored || !colorRestored) {
    KLOG_ERROR(UPSCALER, "Failed to convert texture for upscaling: %s",
               SDL_GetError());
    return false;
  }
  return true;
}

} // namespace Kotonoha::Detail

using Kotonoha::Detail::CopyTextureToTarget;
using Kotonoha::Detail::AdvanceUpscalingRevision;
using Kotonoha::Detail::g_failedUpscalerRevision;
using Kotonoha::Detail::g_loggedUnavailableBackend;
using Kotonoha::Detail::g_upscalerType;
using Kotonoha::Detail::g_upscalingEnabled;
using Kotonoha::Detail::g_upscalingFactor;
using Kotonoha::Detail::g_upscalingPreset;
using Kotonoha::Detail::g_upscalingRevision;
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
using Kotonoha::Detail::g_observedMpvRetryDeadlineMs;
#endif

extern "C" {

bool KotonohaUpscalingIsEnabled(void) { return g_upscalingEnabled; }
void KotonohaUpscalingSetEnabled(bool enabled) {
  if (g_upscalingEnabled != enabled) {
    g_upscalingEnabled = enabled;
    AdvanceUpscalingRevision();
  }
}

Kotonoha_UpscalerPreset KotonohaUpscalingGetPreset(void) {
  return g_upscalingPreset;
}

const char *KotonohaUpscalingGetPresetName(Kotonoha_UpscalerPreset preset) {
  switch (preset) {
  case KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16:
    return "Anime Light (ArtCNN C4F16)";
  case KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16_DS:
    return "Anime Clean (ArtCNN C4F16 DS)";
  case KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F32:
    return "Anime Quality (ArtCNN C4F32)";
  case KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K:
  default:
    return "Legacy Anime4K B-S";
  }
}

void KotonohaUpscalingSetPreset(Kotonoha_UpscalerPreset preset) {
  if (preset < KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K ||
      preset >= KOTONOHA_UPSCALER_PRESET_COUNT) {
    preset = KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K;
  }
  if (g_upscalingPreset == preset) {
    return;
  }
  g_upscalingPreset = preset;
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
  KotonohaMPVUpscalerSetPreset(static_cast<int>(preset));
#endif
  // Static image/Canvas caches are keyed by this revision. A model switch must
  // rebuild them even when their source texture/serial has not changed.
  AdvanceUpscalingRevision();
}

void KotonohaUpscalingSetCustomShaderFiles(const char *files) {
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
  KotonohaMPVUpscalerSetFiles(files);
#else
  (void)files;
#endif
  AdvanceUpscalingRevision();
}
Uint64 KotonohaUpscalingGetRevision(void) {
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
  if (g_upscalerType == KOTONOHA_UPSCALER_MPV_COMPATIBLE) {
    const Uint64 retryDeadline = KotonohaMPVUpscalerRetryDeadlineMs();
    if (retryDeadline == 0) {
      g_observedMpvRetryDeadlineMs = 0;
    } else if (retryDeadline != g_observedMpvRetryDeadlineMs &&
               SDL_GetTicks() >= retryDeadline) {
      // A cached static texture may currently contain the temporary SDL copy.
      // Advance the generic revision exactly once for this retry deadline so
      // CanvasImage/Video re-enter the backend when the retry becomes due.
      g_observedMpvRetryDeadlineMs = retryDeadline;
      AdvanceUpscalingRevision();
    }
  }
#endif
  return g_upscalingRevision;
}

bool KotonohaUpscalingInit(SDL_Renderer *renderer, const char *name,
                           Kotonoha_Config *config) {
  if (config == NULL) {
    return false;
  }
  g_upscalingEnabled = config->upscalingEnabled;
  g_upscalingFactor = config->scalerFactor;
  const int configuredPreset = config->upscalerPreset.load();
  g_upscalingPreset =
      configuredPreset >= KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K &&
              configuredPreset < KOTONOHA_UPSCALER_PRESET_COUNT
          ? static_cast<Kotonoha_UpscalerPreset>(configuredPreset)
          : KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K;
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
  KotonohaMPVUpscalerSetPreset(static_cast<int>(g_upscalingPreset));
#endif
  AdvanceUpscalingRevision();

  if (name != NULL && SDL_strcasecmp(name, "Auto") != 0) {
    if (!Kotonoha_SetUpscalerByName(name)) {
      KLOG_WARN(UPSCALER, "Requested upscaler '%s' is not supported.", name);
      g_upscalerType = KOTONOHA_UPSCALER_NONE;
      KLOG_WARN(UPSCALER,
                "Using SDL texture scaling fallback (enabled=%d factor=%.2f).",
                g_upscalingEnabled ? 1 : 0, g_upscalingFactor);
      return true;
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
    } else if (isOpenGL && Kotonoha_HasMPVUpscaler()) {
      g_upscalerType = KOTONOHA_UPSCALER_MPV_COMPATIBLE;
    } else {
      g_upscalerType = KOTONOHA_UPSCALER_NONE;
    }
  }

  const char *rendererName =
      renderer != NULL ? SDL_GetRendererName(renderer) : "<unavailable>";
  if (g_upscalerType == KOTONOHA_UPSCALER_METALFX) {
#if defined(KOTONOHA_HAS_METALFX)
    if (KotonohaMetalFXInit(renderer, config)) {
      KLOG_INFO(UPSCALER,
                "MetalFX ready (renderer=%s enabled=%d factor=%.2f)",
                rendererName, g_upscalingEnabled ? 1 : 0, g_upscalingFactor);
      return true;
    }
#endif
    KLOG_WARN(UPSCALER,
              "MetalFX initialization failed; falling back to MPVCompatibleUpscaler.");
    g_upscalerType = KOTONOHA_UPSCALER_MPV_COMPATIBLE;
  }

  if (g_upscalerType == KOTONOHA_UPSCALER_MPV_COMPATIBLE) {
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
    if (KotonohaMPVUpscalerInit(renderer, config)) {
      KLOG_INFO(UPSCALER,
                "MPVCompatibleUpscaler ready (renderer=%s enabled=%d factor=%.2f)",
                rendererName, g_upscalingEnabled ? 1 : 0, g_upscalingFactor);
      return true;
    }
    // Keep the selected backend alive after a transient initialization failure.
    // MPVCompatibleUpscaler owns a bounded retry/fallback path; changing the
    // generic type to NONE here would prevent that retry path from ever being
    // called again when an OpenGL context becomes available later.
    KLOG_WARN(UPSCALER,
              "MPVCompatibleUpscaler initialization failed; using SDL texture "
              "scaling while the backend retries.");
    return true;
#else
    KLOG_WARN(UPSCALER, "MPVCompatibleUpscaler is not available in this build.");
#endif
  }

  KLOG_WARN(UPSCALER,
            "No shader upscaler initialized (renderer=%s enabled=%d factor=%.2f). "
            "When enabled, using SDL texture scaling.",
            rendererName, g_upscalingEnabled ? 1 : 0, g_upscalingFactor);
  g_upscalerType = KOTONOHA_UPSCALER_NONE;
  return true;
}

void KotonohaUpscalingInitShutdown(void) {
#if defined(KOTONOHA_HAS_METALFX)
  KotonohaMetalFXShutdown();
#endif
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
  KotonohaMPVUpscalerShutdown();
#endif
  g_upscalerType = KOTONOHA_UPSCALER_NONE;
  AdvanceUpscalingRevision();
}

bool Kotonoha_HasUpscaler(void) {
  return Kotonoha_HasMetalFX() || Kotonoha_HasMPVUpscaler();
}

bool Kotonoha_HasMetalFX(void) {
#if defined(KOTONOHA_HAS_METALFX)
  return true;
#else
  return false;
#endif
}

bool Kotonoha_HasMPVUpscaler(void) {
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
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
  case KOTONOHA_UPSCALER_MPV_COMPATIBLE:
    return "MPVCompatibleUpscaler";
  default:
    return "None";
  }
}

void Kotonoha_SetUpscalerType(Kotonoha_UpscalerType type) {
  if (g_upscalerType != type) {
    g_upscalerType = type;
    AdvanceUpscalingRevision();
  }
}

bool Kotonoha_SetUpscalerByName(const char *name) {
  if (name == NULL)
    return false;

  if (SDL_strcasecmp(name, "MetalFX") == 0) {
    if (Kotonoha_HasMetalFX()) {
      Kotonoha_SetUpscalerType(KOTONOHA_UPSCALER_METALFX);
      return true;
    }
    return false;
  }
  if (SDL_strcasecmp(name, "MPVCompatibleUpscaler") == 0 ||
      SDL_strcasecmp(name, "Anime4K") == 0 ||
      SDL_strcasecmp(name, "OpenGL") == 0 ||
      SDL_strcasecmp(name, "GLES") == 0) {
    if (Kotonoha_HasMPVUpscaler()) {
      Kotonoha_SetUpscalerType(KOTONOHA_UPSCALER_MPV_COMPATIBLE);
      return true;
    }
    return false;
  }
  if (SDL_strcasecmp(name, "None") == 0 || SDL_strcasecmp(name, "Off") == 0) {
    Kotonoha_SetUpscalerType(KOTONOHA_UPSCALER_NONE);
    return true;
  }
  if (SDL_strcasecmp(name, "Auto") == 0) {
    if (Kotonoha_HasMetalFX()) {
      Kotonoha_SetUpscalerType(KOTONOHA_UPSCALER_METALFX);
    } else if (Kotonoha_HasMPVUpscaler()) {
      Kotonoha_SetUpscalerType(KOTONOHA_UPSCALER_MPV_COMPATIBLE);
    } else {
      Kotonoha_SetUpscalerType(KOTONOHA_UPSCALER_NONE);
    }
    return true;
  }
  return false;
}

float KotonohaUpscalingGetFactor(void) { return g_upscalingFactor; }

void KotonohaUpscalingSetFactor(float factor) {
  if (g_upscalingFactor != factor) {
    g_upscalingFactor = factor;
    AdvanceUpscalingRevision();
  }
}

void KotonohaUpscalingReleaseTexture(Kotonoha_UpscaledTexture *cached) {
  if (cached == NULL) {
    return;
  }
  if (cached->intermediate != NULL) {
    SDL_DestroyTexture(cached->intermediate);
  }
  if (cached->output != NULL) {
    SDL_DestroyTexture(cached->output);
  }
  SDL_zero(*cached);
}

bool KotonohaUpscalingUpdateTexture(SDL_Renderer *renderer,
                                    SDL_Texture *source,
                                    Uint64 sourceSerial,
                                    Kotonoha_UpscaledTexture *cached,
                                    bool *textureChanged) {
  using Kotonoha::Detail::ConvertTextureForUpscaling;
  using Kotonoha::Detail::IsSupportedUpscaleInputFormat;
  using Kotonoha::Detail::StoreUpscaledTextureMetadata;

  if (textureChanged != NULL) {
    *textureChanged = false;
  }
  if (renderer == NULL || source == NULL || cached == NULL) {
    SDL_SetError("Upscaling texture update requires valid arguments");
    return false;
  }

  float textureWidth = 0.0f;
  float textureHeight = 0.0f;
  if (!SDL_GetTextureSize(source, &textureWidth, &textureHeight) ||
      textureWidth <= 0.0f || textureHeight <= 0.0f ||
      textureWidth > static_cast<float>(INT_MAX) ||
      textureHeight > static_cast<float>(INT_MAX)) {
    return false;
  }

  const int width = static_cast<int>(textureWidth);
  const int height = static_cast<int>(textureHeight);
  const Uint32 format = static_cast<Uint32>(source->format);
  const Uint64 revision = g_upscalingRevision;
  const float factor = g_upscalingFactor;
  const bool active = g_upscalingEnabled && factor > 1.0f;
  const bool hadOutput = cached->output != NULL;

  if (!active) {
    KotonohaUpscalingReleaseTexture(cached);
    StoreUpscaledTextureMetadata(cached, sourceSerial, revision, width, height,
                                 format, factor);
    if (textureChanged != NULL) {
      *textureChanged = hadOutput;
    }
    return false;
  }

  const bool metadataMatches =
      cached->initialized && cached->sourceSerial == sourceSerial &&
      cached->settingsRevision == revision && cached->sourceWidth == width &&
      cached->sourceHeight == height && cached->sourceFormat == format &&
      cached->factor == factor;
  if (metadataMatches) {
    return cached->output != NULL;
  }

  const bool needsIntermediate = !IsSupportedUpscaleInputFormat(source->format);
  const bool outputSizeValid =
      width <= static_cast<int>(static_cast<float>(INT_MAX) / factor) &&
      height <= static_cast<int>(static_cast<float>(INT_MAX) / factor);
  if (!outputSizeValid) {
    SDL_SetError("Upscaling output dimensions exceed supported limits");
    KotonohaUpscalingReleaseTexture(cached);
    StoreUpscaledTextureMetadata(cached, sourceSerial, revision, width, height,
                                 format, factor);
    if (textureChanged != NULL) {
      *textureChanged = hadOutput;
    }
    return false;
  }

  const int outputWidth = static_cast<int>(std::lround(width * factor));
  const int outputHeight = static_cast<int>(std::lround(height * factor));
  if (outputWidth <= width || outputHeight <= height) {
    SDL_SetError("Upscaling factor produced invalid output dimensions");
    KotonohaUpscalingReleaseTexture(cached);
    StoreUpscaledTextureMetadata(cached, sourceSerial, revision, width, height,
                                 format, factor);
    if (textureChanged != NULL) {
      *textureChanged = hadOutput;
    }
    return false;
  }

  const bool recreateResources =
      cached->output == NULL || cached->sourceWidth != width ||
      cached->sourceHeight != height || cached->sourceFormat != format ||
      cached->factor != factor ||
      (cached->intermediate != NULL) != needsIntermediate;
  if (recreateResources) {
    KotonohaUpscalingReleaseTexture(cached);
    if (needsIntermediate) {
      cached->intermediate =
          SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                            SDL_TEXTUREACCESS_TARGET, width, height);
      if (cached->intermediate == NULL ||
          !SDL_SetTextureBlendMode(cached->intermediate, SDL_BLENDMODE_NONE)) {
        KLOG_ERROR(UPSCALER,
                   "Failed to create intermediate texture for %dx%d source: %s",
                   width, height, SDL_GetError());
        KotonohaUpscalingReleaseTexture(cached);
        StoreUpscaledTextureMetadata(cached, sourceSerial, revision, width,
                                     height, format, factor);
        if (textureChanged != NULL) {
          *textureChanged = hadOutput;
        }
        return false;
      }
    }

    cached->output =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                          SDL_TEXTUREACCESS_TARGET, outputWidth, outputHeight);
    if (cached->output == NULL ||
        !SDL_SetTextureBlendMode(cached->output, SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(cached->output, SDL_SCALEMODE_LINEAR)) {
      KLOG_ERROR(UPSCALER,
                 "Failed to create %dx%d upscaled texture: %s", outputWidth,
                 outputHeight, SDL_GetError());
      KotonohaUpscalingReleaseTexture(cached);
      StoreUpscaledTextureMetadata(cached, sourceSerial, revision, width,
                                   height, format, factor);
      if (textureChanged != NULL) {
        *textureChanged = hadOutput;
      }
      return false;
    }
  }

  // Callers tint at draw time (colour/alpha mod on the source); the cached
  // output must hold the untinted picture, so copy it with neutral mods.
  struct NeutralMods {
    SDL_Texture *texture;
    float r = 1, g = 1, b = 1, a = 1;
    explicit NeutralMods(SDL_Texture *t) : texture(t) {
      SDL_GetTextureColorModFloat(texture, &r, &g, &b);
      SDL_GetTextureAlphaModFloat(texture, &a);
      SDL_SetTextureColorModFloat(texture, 1, 1, 1);
      SDL_SetTextureAlphaModFloat(texture, 1);
    }
    ~NeutralMods() {
      SDL_SetTextureColorModFloat(texture, r, g, b);
      SDL_SetTextureAlphaModFloat(texture, a);
    }
  } neutralMods(source);

  SDL_Texture *upscaleInput = source;
  if (needsIntermediate) {
    if (cached->intermediate == NULL ||
        !ConvertTextureForUpscaling(renderer, source, cached->intermediate)) {
      KotonohaUpscalingReleaseTexture(cached);
      StoreUpscaledTextureMetadata(cached, sourceSerial, revision, width,
                                   height, format, factor);
      if (textureChanged != NULL) {
        *textureChanged = hadOutput;
      }
      return false;
    }
    upscaleInput = cached->intermediate;
  }

  bool upscaleSucceeded = false;
  if (g_upscalerType != KOTONOHA_UPSCALER_NONE &&
      g_failedUpscalerRevision != revision) {
    SDL_ClearError();
    upscaleSucceeded =
        Kotonoha_UpscaleSDLTextureInto(renderer, upscaleInput, cached->output);
    if (!upscaleSucceeded) {
      const char *failure = SDL_GetError();
      KLOG_WARN(UPSCALER,
                "%s failed for texture %dx%d format=%s factor=%.2f: %s; "
                "using SDL texture scaling until settings change",
                Kotonoha_GetUpscalerName(), width, height,
                SDL_GetPixelFormatName(source->format), factor,
                failure != NULL && *failure != '\0' ? failure
                                                     : "no backend error");
      g_failedUpscalerRevision = revision;
    }
  } else if (g_upscalerType == KOTONOHA_UPSCALER_NONE &&
             !g_loggedUnavailableBackend) {
    KLOG_INFO(UPSCALER,
              "No shader upscaler selected; using SDL texture scaling "
              "(factor=%.2f).",
              factor);
    g_loggedUnavailableBackend = true;
  }

  if (!upscaleSucceeded) {
    upscaleSucceeded =
        CopyTextureToTarget(renderer, upscaleInput, cached->output);
  }
  const Uint64 currentRevision = g_upscalingRevision;
  if (!upscaleSucceeded || currentRevision != revision ||
      !g_upscalingEnabled || g_upscalingFactor <= 1.0f) {
    KotonohaUpscalingReleaseTexture(cached);
    StoreUpscaledTextureMetadata(cached, sourceSerial, currentRevision, width,
                                 height, format, g_upscalingFactor);
    if (textureChanged != NULL) {
      *textureChanged = true;
    }
    return false;
  }

  StoreUpscaledTextureMetadata(cached, sourceSerial, currentRevision, width,
                               height, format, factor);
  if (textureChanged != NULL) {
    *textureChanged = true;
  }
  return true;
}

bool Kotonoha_UpscaleSDLTextureInto(SDL_Renderer *renderer,
                                    SDL_Texture *inputTexture,
                                    SDL_Texture *outputTexture) {
  if (g_upscalerType == KOTONOHA_UPSCALER_METALFX) {
#if defined(KOTONOHA_HAS_METALFX)
    if (KotonohaMetalFXUpscaleSDLTextureInto(renderer, inputTexture,
                                             outputTexture)) {
      return true;
    }
    KLOG_WARN(UPSCALER, "MetalFX texture upscale failed: %s", SDL_GetError());
#endif
  } else if (g_upscalerType == KOTONOHA_UPSCALER_MPV_COMPATIBLE) {
#if defined(KOTONOHA_HAS_MPV_UPSCALER)
    if (KotonohaMPVUpscalerUpscaleSDLTextureInto(renderer, inputTexture,
                                             outputTexture)) {
      return true;
    }
    KLOG_WARN(UPSCALER, "MPVCompatibleUpscaler texture upscale failed: %s", SDL_GetError());
#endif
  }

  return false;
}

} // extern "C"
