#pragma once

#include <Kotonoha/core/Settings.hpp>
#include <SDL3/SDL_video.h>

#include <string>
#include <vector>

namespace Kotonoha {

// Physical display modes offered by the platform, independently of any game.
// The fractional refresh is retained so 59.94 and 60 Hz stay distinct.
struct DisplayModeChoice {
  int width = 0;
  int height = 0;
  float refreshRate = 0.0f;
};

class DisplayModeCatalog final {
public:
  // On Windows, the monitor's 32bpp DEVMODE table is authoritative. Use SDL
  // when that native enumeration is unavailable or on other platforms.
  // Requires an initialized SDL video subsystem; call on the SDL main thread.
  static std::vector<DisplayModeChoice> Enumerate(SDL_DisplayID display);

  // These helpers are independent of SDL video initialization and testable
  // against synthetic catalogs on every supported platform.
  static bool AddUnique(std::vector<DisplayModeChoice> &modes,
                        DisplayModeChoice mode);
  static float NormalizeWindowsRefresh(unsigned int frequency);
  static void Sort(std::vector<DisplayModeChoice> &modes);
  static int FindSelected(const std::vector<DisplayModeChoice> &modes,
                          int width, int height, float refreshRate);
  static std::string Label(const DisplayModeChoice &mode);
  static bool Select(EngineSettings &settings, DisplayModeChoice mode,
                     const SDL_DisplayMode *desktop);
};

} // namespace Kotonoha
