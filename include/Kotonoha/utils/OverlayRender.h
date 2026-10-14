#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#ifdef __cplusplus
extern "C" {
#endif

bool KotonohaOverlayGetMetrics(SDL_Window *window, SDL_Renderer *render,
                               int *outputWidth, int *outputHeight,
                               float *windowScale);

bool KotonohaOverlayCreateTextTexture(SDL_Renderer *render, TTF_Font *font,
                                      const char *text, SDL_Color color,
                                      SDL_Texture **texture, float *textWidth,
                                      float *textHeight);

#ifdef __cplusplus
}
#endif