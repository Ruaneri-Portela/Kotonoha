
#include <Kotonoha/renders/TimestampRender.h>
#include <Kotonoha/utils/Log.h>
#include <Kotonoha/utils/OverlayRender.h>
#include <Kotonoha/utils/Time.h>

struct Timestamp_common {
  SDL_Texture *texture;
  SDL_Texture *shadow;
  SDL_FRect rect;
  SDL_Color color;
  SDL_Color shadowColor;
  char text[64]; // CORRIGIDO: Era 'char text', causando Estourameto de Buffer
  Uint64 lastTime;
  float windowScale;
};

static struct Timestamp_common *timestampCommon = NULL;

void KotonohaTimestampRenderShutdown(void) {
  if (timestampCommon == NULL) {
    return;
  }
  if (timestampCommon->texture != NULL) {
    SDL_DestroyTexture(timestampCommon->texture);
  }
  if (timestampCommon->shadow != NULL) {
    SDL_DestroyTexture(timestampCommon->shadow);
  }
  SDL_free(timestampCommon);
  timestampCommon = NULL;
}

static bool UpdateTimestampTexture(SDL_Renderer *render, TTF_Font *font) {
  if (render == NULL || font == NULL || timestampCommon == NULL) {
    return false;
  }
  SDL_Texture *newTexture = NULL;
  SDL_Texture *newShadow = NULL;
  float textWidth = 0.0f;
  float textHeight = 0.0f;

  if (!KotonohaOverlayCreateTextTexture(render, font, timestampCommon->text,
                                        timestampCommon->shadowColor,
                                        &newShadow, &textWidth, &textHeight)) {
    return false;
  }

  if (!KotonohaOverlayCreateTextTexture(render, font, timestampCommon->text,
                                        timestampCommon->color, &newTexture,
                                        &textWidth, &textHeight)) {
    SDL_DestroyTexture(newShadow);
    return false;
  }

  if (timestampCommon->texture != NULL) {
    SDL_DestroyTexture(timestampCommon->texture);
  }
  if (timestampCommon->shadow != NULL) {
    SDL_DestroyTexture(timestampCommon->shadow);
  }

  timestampCommon->texture = newTexture;
  timestampCommon->shadow = newShadow;

  // AJUSTE DE ESCALA: Mantém o tamanho lógico constante dividindo pela escala
  timestampCommon->rect.w = textWidth;
  timestampCommon->rect.h = textHeight;

  return true;
}

enum Kotonoha_Scene_Status KotonohaTimestampRender(KOTONOHA_SCENE_CALL) {
  (void)target;
  if (userData == NULL) {
    return KOTONOHA_SCENE_NULL;
  }

  void **args = (void **)userData;
  Kotonoha_Time *timestamp = (Kotonoha_Time *)args[0];
  TTF_Font *font = (TTF_Font *)args[1];

  if (timestamp == NULL || font == NULL) {
    return KOTONOHA_SCENE_NULL;
  }

  int outputWidth = 0;
  int outputHeight = 0;
  float windowScale = 1.0f;

  if (!KotonohaOverlayGetMetrics(window, render, &outputWidth, &outputHeight,
                                 &windowScale)) {
    return KOTONOHA_SCENE_NULL;
  }

  // Rasterize at physical pixels so the overlay is never upscaled.
  TTF_SetFontSize(font, 28.0f * windowScale);

  if (timestampCommon == NULL) {
    timestampCommon = (struct Timestamp_common *)SDL_calloc(
        1, sizeof(struct Timestamp_common));
    if (timestampCommon == NULL) {
      KLOG_ERROR(RENDER, "Failed to allocate timestamp renderer state");
      return KOTONOHA_SCENE_NULL;
    }
    timestampCommon->color = (SDL_Color){0, 255, 255, 255};
    timestampCommon->shadowColor = (SDL_Color){0, 0, 0, 160};
    SDL_snprintf(timestampCommon->text, sizeof(timestampCommon->text),
                 "00:00:00 :TS");
    timestampCommon->windowScale = windowScale;

    if (!UpdateTimestampTexture(render, font)) {
      SDL_free(timestampCommon);
      timestampCommon = NULL;
      return KOTONOHA_SCENE_NULL;
    }
  } else if (timestampCommon->windowScale != windowScale) {
    timestampCommon->windowScale = windowScale;
    if (!UpdateTimestampTexture(render, font)) {
      return KOTONOHA_SCENE_NULL;
    }
  }

  if (SDL_GetTicks() - timestampCommon->lastTime >= 100) {
    const Uint64 time = KotonohaTimeGet(timestamp);
    const Uint64 totalSeconds = time / 1000;
    const Uint64 milliseconds = time % 1000;
    const Uint64 minutes = totalSeconds / 60;
    const Uint64 seconds = totalSeconds % 60;
    const Uint64 deciseconds = milliseconds / 10;

    SDL_snprintf(timestampCommon->text, sizeof(timestampCommon->text),
                 "%02lu:%02lu:%02lu :TS", (unsigned long)minutes,
                 (unsigned long)seconds, (unsigned long)deciseconds);

    if (!UpdateTimestampTexture(render, font)) {
      return KOTONOHA_SCENE_NULL;
    }
    timestampCommon->lastTime = SDL_GetTicks();
  }

  // Posição no canto superior direito mantendo margem física proporcional
  timestampCommon->rect.x = (float)outputWidth - timestampCommon->rect.w - 8.0f;
  timestampCommon->rect.y = 8.0f;

  if (timestampCommon->rect.x < 0.0f) {
    timestampCommon->rect.x = 0.0f;
  }
  if (timestampCommon->rect.y + timestampCommon->rect.h > (float)outputHeight) {
    timestampCommon->rect.y = (float)outputHeight - timestampCommon->rect.h;
    if (timestampCommon->rect.y < 0.0f) {
      timestampCommon->rect.y = 0.0f;
    }
  }

  if (timestampCommon->shadow != NULL) {
    SDL_FRect shadowRect = timestampCommon->rect;
    shadowRect.x += 2.0f;
    shadowRect.y += 2.0f;
    if (!SDL_RenderTexture(render, timestampCommon->shadow, NULL,
                           &shadowRect)) {
      KLOG_ERROR(RENDER, "Failed to draw timestamp shadow: %s", SDL_GetError());
    }
  }

  if (timestampCommon->texture != NULL &&
      !SDL_RenderTexture(render, timestampCommon->texture, NULL,
                         &timestampCommon->rect)) {
    KLOG_ERROR(RENDER, "Failed to draw timestamp text: %s", SDL_GetError());
  }

  return KOTONOHA_SCENE_DRAW_OVERLAYED;
}