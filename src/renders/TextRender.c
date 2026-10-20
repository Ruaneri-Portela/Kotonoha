#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/utils/Log.h>

#include <SDL3/SDL_render.h>

// =============================================================================
// Internal helpers
// =============================================================================

static SDL_Texture *
KotonohaTextRenderCreateGlyphTexture(SDL_Renderer *render,
                                     const ASS_Image *image) {
  if (render == NULL || image == NULL || image->bitmap == NULL ||
      image->w <= 0 || image->h <= 0) {
    return NULL;
  }

  SDL_Texture *texture =
      SDL_CreateTexture(render, SDL_PIXELFORMAT_RGBA32,
                        SDL_TEXTUREACCESS_STREAMING, image->w, image->h);

  if (texture == NULL) {
    KLOG_ERROR(TEXT, "SDL_CreateTexture for ASS glyph failed: %s",
               SDL_GetError());

    return NULL;
  }

  if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
    KLOG_ERROR(TEXT, "SDL_SetTextureBlendMode for ASS glyph failed: %s",
               SDL_GetError());

    SDL_DestroyTexture(texture);
    return NULL;
  }

  Uint8 *pixels = NULL;
  int pitch = 0;

  if (!SDL_LockTexture(texture, NULL, (void **)&pixels, &pitch)) {
    KLOG_ERROR(TEXT, "SDL_LockTexture for ASS glyph failed: %s",
               SDL_GetError());

    SDL_DestroyTexture(texture);
    return NULL;
  }

  // ASS_Image::color tem formato RRGGBBAA, mas AA representa transparência:
  // 0 = totalmente opaco; 255 = totalmente transparente.
  const Uint8 red = (Uint8)((image->color >> 24) & 0xFF);

  const Uint8 green = (Uint8)((image->color >> 16) & 0xFF);

  const Uint8 blue = (Uint8)((image->color >> 8) & 0xFF);

  const Uint8 colorAlpha = (Uint8)(255 - (image->color & 0xFF));

  const unsigned char *bitmap = image->bitmap;

  for (int y = 0; y < image->h; ++y) {
    Uint8 *rowPixels = pixels + (y * pitch);

    for (int x = 0; x < image->w; ++x) {
      const Uint8 coverage = bitmap[x];

      const Uint8 alpha = (Uint8)((coverage * colorAlpha) / 255);

      const int pixelOffset = x * 4;

      rowPixels[pixelOffset + 0] = red;
      rowPixels[pixelOffset + 1] = green;
      rowPixels[pixelOffset + 2] = blue;
      rowPixels[pixelOffset + 3] = alpha;
    }

    bitmap += image->stride;
  }

  SDL_UnlockTexture(texture);

  return texture;
}

static bool KotonohaTextRenderGetTargetSize(SDL_Renderer *render,
                                            SDL_Texture *target, int *width,
                                            int *height) {
  if (render == NULL || width == NULL || height == NULL) {
    return false;
  }

  *width = 0;
  *height = 0;

  // Quando há uma textura target, as dimensões reais da renderização vêm
  // dela. target normalmente é a textura final da sua pipeline.
  if (target != NULL) {
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;

    if (!SDL_GetTextureSize(target, &textureWidth, &textureHeight)) {
      KLOG_ERROR(TEXT, "SDL_GetTextureSize for subtitle target failed: %s",
                 SDL_GetError());

      return false;
    }

    *width = (int)textureWidth;
    *height = (int)textureHeight;
  } else {
    // NULL como target significa que a renderização é feita diretamente
    // para a janela associada ao SDL_Renderer.
    if (!SDL_GetRenderOutputSize(render, width, height)) {
      KLOG_ERROR(TEXT, "SDL_GetRenderOutputSize for subtitles failed: %s",
                 SDL_GetError());

      return false;
    }
  }

  return *width > 0 && *height > 0;
}

// =============================================================================
// Public API
// =============================================================================

Kotonoha_Subtitles *KotonohaTextRenderInit(Kotonoha_Time *time,
                                           Kotonoha_Game *gameCtx) {
  if (time == NULL || gameCtx == NULL) {
    KLOG_ERROR(TEXT, "KotonohaTextRenderInit received invalid arguments.");

    return NULL;
  }

  Kotonoha_Subtitles *object =
      (Kotonoha_Subtitles *)SDL_calloc(1, sizeof(Kotonoha_Subtitles));

  if (object == NULL) {
    KLOG_ERROR(TEXT, "Failed to allocate Kotonoha_subtitles.");

    return NULL;
  }

  object->ass_library = gameCtx->ass_library;

  object->ass_renderer = gameCtx->ass_renderer;

  object->track = gameCtx->ass_track;

  object->time = time;

  return object;
}

void KotonohaTextRenderShutdown(struct Kotonoha_Subtitles **object) {
  if (object == NULL || *object == NULL) {
    return;
  }

  // Não há mais subTexture persistente para destruir.
  //
  // As texturas temporárias de glyph são criadas e destruídas no mesmo
  // frame, dentro de KotonohaTextRenderDraw().
  SDL_free(*object);
  *object = NULL;
}

enum Kotonoha_Scene_Status KotonohaTextRenderDraw(KOTONOHA_SCENE_CALL) {
  if (render == NULL || window == NULL || userData == NULL) {
    return KOTONOHA_SCENE_FATAL_ERROR;
  }

  struct Kotonoha_Subtitles *environment =
      (struct Kotonoha_Subtitles *)userData;

  if (environment->ass_renderer == NULL || environment->track == NULL ||
      environment->time == NULL) {
    return KOTONOHA_SCENE_NULL;
  }

  int targetWidth = 0;
  int targetHeight = 0;

  if (!KotonohaTextRenderGetTargetSize(render, target, &targetWidth,
                                       &targetHeight)) {
    return KOTONOHA_SCENE_WAITING;
  }

  // Salva o target ativo para não quebrar a pipeline caso o chamador tenha
  // outro render target selecionado.
  SDL_Texture *previousTarget = SDL_GetRenderTarget(render);

  // Ativa a textura de frame onde a legenda deve ser desenhada.
  //
  // target == NULL é válido: nesse caso a renderização vai para a janela.
  if (previousTarget != target) {
    if (!SDL_SetRenderTarget(render, target)) {
      KLOG_ERROR(TEXT, "Failed to activate subtitle target: %s",
                 SDL_GetError());

      return KOTONOHA_SCENE_FATAL_ERROR;
    }
  }

  // Configura libass usando as dimensões físicas do render target.
  ass_set_storage_size(environment->ass_renderer, targetWidth, targetHeight);

  ass_set_frame_size(environment->ass_renderer, targetWidth, targetHeight);

  int assChanged = 0;

  ASS_Image *frame =
      ass_render_frame(environment->ass_renderer, environment->track,
                       KotonohaTimeGet(environment->time), &assChanged);

  if (assChanged == 0) {
    if (previousTarget != target) {
      SDL_SetRenderTarget(render, previousTarget);
    }

    // COMPLETE (not WAITING) tells the canvas to drop its retained frame;
    // otherwise the last subtitle would be redrawn after it ended.
    return frame != NULL ? KOTONOHA_SCENE_DRAW_OVERLAYED
                         : KOTONOHA_SCENE_COMPLETE;
  }

  // Não há glyphs ativos neste timestamp.
  if (frame == NULL) {
    if (previousTarget != target) {
      SDL_SetRenderTarget(render, previousTarget);
    }

    return KOTONOHA_SCENE_COMPLETE;
  }

  Uint8 previousRed = 0;
  Uint8 previousGreen = 0;
  Uint8 previousBlue = 0;
  Uint8 previousAlpha = 0;
  if (!SDL_GetRenderDrawColor(render, &previousRed, &previousGreen,
                              &previousBlue, &previousAlpha) ||
      !SDL_SetRenderDrawColor(render, 0, 0, 0, 0)) {
    KLOG_ERROR(TEXT, "Failed to prepare transparent subtitle canvas: %s",
               SDL_GetError());
    if (previousTarget != target) {
      SDL_SetRenderTarget(render, previousTarget);
    }
    return KOTONOHA_SCENE_FATAL_ERROR;
  }

  const bool cleared = SDL_RenderClear(render);
  const bool restoredDrawColor = SDL_SetRenderDrawColor(
      render, previousRed, previousGreen, previousBlue, previousAlpha);
  if (!cleared || !restoredDrawColor) {
    KLOG_ERROR(TEXT, "Failed to clear transparent subtitle canvas: %s",
               SDL_GetError());
    if (previousTarget != target) {
      SDL_SetRenderTarget(render, previousTarget);
    }
    return KOTONOHA_SCENE_FATAL_ERROR;
  }

  bool renderedAny = false;

  for (ASS_Image *image = frame; image != NULL; image = image->next) {
    SDL_Texture *glyphTexture =
        KotonohaTextRenderCreateGlyphTexture(render, image);

    if (glyphTexture == NULL) {
      continue;
    }

    const SDL_FRect destination = {(float)image->dst_x, (float)image->dst_y,
                                   (float)image->w, (float)image->h};

    if (!SDL_RenderTexture(render, glyphTexture, NULL, &destination)) {
      KLOG_ERROR(TEXT, "Failed to draw ASS glyph: %s", SDL_GetError());

      SDL_DestroyTexture(glyphTexture);

      if (previousTarget != target) {
        SDL_SetRenderTarget(render, previousTarget);
      }

      return KOTONOHA_SCENE_FATAL_ERROR;
    }

    renderedAny = true;

    SDL_DestroyTexture(glyphTexture);
  }

  // Restaura o target que estava ativo antes de entrar na scene.
  if (previousTarget != target) {
    if (!SDL_SetRenderTarget(render, previousTarget)) {
      KLOG_ERROR(TEXT, "Failed to restore render target after subtitles: %s",
                 SDL_GetError());

      return KOTONOHA_SCENE_FATAL_ERROR;
    }
  }

  return renderedAny ? KOTONOHA_SCENE_DRAW_OVERLAYED
                     : KOTONOHA_SCENE_COMPLETE;
}