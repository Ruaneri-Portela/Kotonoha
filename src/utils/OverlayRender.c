#include <Kotonoha/utils/Log.h>
#include <Kotonoha/utils/OverlayRender.h>

bool KotonohaOverlayGetMetrics(SDL_Window *window, SDL_Renderer *render,
                               int *outputWidth, int *outputHeight,
                               float *windowScale) {
  if (window == NULL || render == NULL || outputWidth == NULL ||
      outputHeight == NULL || windowScale == NULL) {
    KLOG_ERROR(RENDER, "Invalid arguments for overlay render metrics");
    return false;
  }

  if (!SDL_GetCurrentRenderOutputSize(render, outputWidth, outputHeight)) {
    KLOG_ERROR(RENDER, "Failed to get renderer output size: %s", SDL_GetError());
    return false;
  }

  *windowScale = SDL_GetWindowDisplayScale(window);
  if (*windowScale <= 0.0f) {
    KLOG_ERROR(RENDER, "Failed to get window display scale: %s", SDL_GetError());
    return false;
  }

  return true;
}

bool KotonohaOverlayCreateTextTexture(SDL_Renderer *render, TTF_Font *font,
                                      const char *text, SDL_Color color,
                                      SDL_Texture **texture, float *textWidth,
                                      float *textHeight) {
  if (render == NULL || font == NULL || text == NULL || texture == NULL ||
      textWidth == NULL || textHeight == NULL) {
    KLOG_ERROR(RENDER, "Invalid arguments for overlay text texture");
    return false;
  }

  const size_t textLength = SDL_strlen(text);
  int measuredWidth = 0;
  int measuredHeight = 0;

  if (!TTF_GetStringSize(font, text, textLength, &measuredWidth,
                         &measuredHeight)) {
    KLOG_ERROR(RENDER, "Failed to measure overlay text: %s",
                 SDL_GetError());
    return false;
  }

  SDL_Surface *surface = TTF_RenderText_Blended(font, text, textLength, color);
  if (surface == NULL) {
    KLOG_ERROR(RENDER, "Failed to render overlay text: %s",
                 SDL_GetError());
    return false;
  }

  SDL_Texture *createdTexture = SDL_CreateTextureFromSurface(render, surface);
  SDL_DestroySurface(surface);

  if (createdTexture == NULL) {
    KLOG_ERROR(RENDER, "Failed to create overlay text texture: %s", SDL_GetError());
    return false;
  }

  *texture = createdTexture;
  *textWidth = (float)measuredWidth;
  *textHeight = (float)measuredHeight;
  return true;
}