#pragma once
#include <SDL3/SDL.h>

namespace Kotonoha {

struct PanelStyle {
  float radius = 12.0f;
  SDL_Color top{22, 26, 48, 215};
  SDL_Color bottom{8, 10, 24, 235};
  SDL_Color border{150, 160, 255, 150};
  float borderWidth = 1.5f;
};

// Rounded vertical-gradient panel with a soft outline. Works in logical or
// output coordinates; restores the renderer blend mode.
void DrawPanel(SDL_Renderer *renderer, const SDL_FRect &rect, const PanelStyle &style = {});

// Full-area translucent black overlay used behind menus and choices.
void DrawDim(SDL_Renderer *renderer, const SDL_FRect &rect, Uint8 alpha);

} // namespace Kotonoha
