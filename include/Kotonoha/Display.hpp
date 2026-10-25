#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include <Kotonoha/Kotonoha.h>

namespace Kotonoha {

enum class DisplayMode : Uint8 {
  Windowed = 0,
  ExclusiveFullscreen = 1,
  BorderlessFullscreen = 2,
};

struct DisplayResolution {
  int width = 0;
  int height = 0;
  float refreshRate = 0.0f;
  float pixelDensity = 1.0f;
};

bool GetDisplayMode(const Kotonoha_Game &context, DisplayMode *mode);

// Physical display mechanism only. Game profiles decide which authored aspect
// they want to present inside the resulting drawable area.
bool SetDisplay(Kotonoha_Game &context, DisplayMode mode,
                SDL_DisplayID targetDisplay = 0, int width = 0,
                int height = 0, float refreshRate = 0.0f);

// Returns unique fullscreen modes ordered from smallest to largest area and
// then by refresh rate. The desktop mode is included when SDL reports it.
std::vector<DisplayResolution>
EnumerateDisplayResolutions(SDL_DisplayID display);

// Resolve a persisted zero-based display index against the displays available
// on this machine. Out-of-range indices fall back to the primary display.
SDL_DisplayID ResolveDisplayIndex(int index);
int DisplayIndexForId(SDL_DisplayID display);

} // namespace Kotonoha
