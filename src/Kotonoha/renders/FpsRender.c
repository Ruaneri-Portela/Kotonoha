#include <Kotonoha/renders/FpsRender.h>
#include <Kotonoha/utils/Log.h>
#include <Kotonoha/utils/OverlayRender.h>

struct FPS_common {
  SDL_Texture *texture;
  SDL_Texture *shadow;
  SDL_FRect rect;
  SDL_Color color;
  SDL_Color shadowColor;
  char text[64]; // CORRIGIDO: Era 'char text', causando Estouro de Buffer
  Uint64 lastTime;
  size_t count;
  float windowScale;
};

static struct FPS_common *fpsCommon = NULL;

static void DestroyFPSTextures(void) {
  if (fpsCommon->shadow != NULL) {
    SDL_DestroyTexture(fpsCommon->shadow);
    fpsCommon->shadow = NULL;
  }
  if (fpsCommon->texture != NULL) {
    SDL_DestroyTexture(fpsCommon->texture);
    fpsCommon->texture = NULL;
  }
}

void KotonohaFPSRenderShutdown(void) {
  if (fpsCommon == NULL) {
    return;
  }
  DestroyFPSTextures();
  SDL_free(fpsCommon);
  fpsCommon = NULL;
}

static bool UpdateFPSTexture(SDL_Renderer *render, TTF_Font *font) {
  if (render == NULL || font == NULL || fpsCommon == NULL) {
    return false;
  }
  SDL_Texture *newShadow = NULL;
  SDL_Texture *newTexture = NULL;
  float textWidth = 0.0f;
  float textHeight = 0.0f;

  if (!KotonohaOverlayCreateTextTexture(render, font, fpsCommon->text,
                                        fpsCommon->shadowColor, &newShadow,
                                        &textWidth, &textHeight)) {
    return false;
  }

  if (!KotonohaOverlayCreateTextTexture(render, font, fpsCommon->text,
                                        fpsCommon->color, &newTexture,
                                        &textWidth, &textHeight)) {
    SDL_DestroyTexture(newShadow);
    return false;
  }

  DestroyFPSTextures();
  fpsCommon->shadow = newShadow;
  fpsCommon->texture = newTexture;

  // AJUSTE DE ESCALA: Mantém o tamanho lógico constante dividindo pela escala
  fpsCommon->rect.w = textWidth;
  fpsCommon->rect.h = textHeight;

  return true;
}

enum Kotonoha_Scene_Status KotonohaFPSRender(KOTONOHA_SCENE_CALL) {
  (void)target;
  TTF_Font *font = (TTF_Font *)userData;
  if (font == NULL) {
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

  if (fpsCommon == NULL) {
    fpsCommon = (struct FPS_common *)SDL_calloc(1, sizeof(struct FPS_common));
    if (fpsCommon == NULL) {
      KLOG_ERROR(RENDER, "Failed to allocate FPS renderer state");
      return KOTONOHA_SCENE_NULL;
    }
    fpsCommon->color = (SDL_Color){255, 0, 255, 255};
    fpsCommon->shadowColor = (SDL_Color){0, 0, 0, 160};
    fpsCommon->lastTime = SDL_GetTicks();
    fpsCommon->windowScale = windowScale;
    SDL_snprintf(fpsCommon->text, sizeof(fpsCommon->text), "FPS: 0 - 0.00ms");

    if (!UpdateFPSTexture(render, font)) {
      SDL_free(fpsCommon);
      fpsCommon = NULL;
      return KOTONOHA_SCENE_NULL;
    }
  } else if (fpsCommon->windowScale != windowScale) {
    fpsCommon->windowScale = windowScale;
    if (!UpdateFPSTexture(render, font)) {
      return KOTONOHA_SCENE_NULL;
    }
  }

  if (SDL_GetTicks() - fpsCommon->lastTime >= 1000) {
    const float milliseconds =
        fpsCommon->count > 0 ? 1000.0f / (float)fpsCommon->count : 0.0f;
    SDL_snprintf(fpsCommon->text, sizeof(fpsCommon->text), "FPS: %zu - %.2fms",
                 fpsCommon->count, milliseconds);

    if (!UpdateFPSTexture(render, font)) {
      return KOTONOHA_SCENE_NULL;
    }
    fpsCommon->count = 0;
    fpsCommon->lastTime = SDL_GetTicks();
  }

  fpsCommon->count++;
  fpsCommon->rect.x = 8.0f;
  fpsCommon->rect.y = 8.0f;

  if (fpsCommon->rect.x + fpsCommon->rect.w > (float)outputWidth) {
    fpsCommon->rect.x = (float)outputWidth - fpsCommon->rect.w;
    if (fpsCommon->rect.x < 0.0f) {
      fpsCommon->rect.x = 0.0f;
    }
  }

  if (fpsCommon->rect.y + fpsCommon->rect.h > (float)outputHeight) {
    fpsCommon->rect.y = (float)outputHeight - fpsCommon->rect.h;
    if (fpsCommon->rect.y < 0.0f) {
      fpsCommon->rect.y = 0.0f;
    }
  }

  if (fpsCommon->shadow != NULL) {
    SDL_FRect shadowRect = fpsCommon->rect;
    shadowRect.x += 2.0f;
    shadowRect.y += 2.0f;
    if (!SDL_RenderTexture(render, fpsCommon->shadow, NULL, &shadowRect)) {
      KLOG_ERROR(RENDER, "Failed to draw FPS shadow: %s", SDL_GetError());
    }
  }

  if (fpsCommon->texture != NULL &&
      !SDL_RenderTexture(render, fpsCommon->texture, NULL, &fpsCommon->rect)) {
    KLOG_ERROR(RENDER, "Failed to draw FPS text: %s", SDL_GetError());
  }

  return KOTONOHA_SCENE_DRAW_OVERLAYED;
}
