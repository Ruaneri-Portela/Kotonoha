#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <utility>
#include <vector>

namespace Kotonoha {
struct SystemInfoSection {
  std::string title;
  std::vector<std::pair<std::string, std::string>> fields;
};
struct SystemInfoSnapshot {
  std::vector<SystemInfoSection> sections;
  std::string text() const;
};
// Calls SDL directly. Unknown GPU model or unavailable memory readings are
// reported as unavailable, never guessed. Snapshot is captured on demand.
SystemInfoSnapshot captureSystemInfo(SDL_Window *window, SDL_Renderer *renderer);
// Shared between the launcher Info tab and debug System Info applet.
void drawSystemInfoPanel(SDL_Window *window, SDL_Renderer *renderer);
} // namespace Kotonoha
