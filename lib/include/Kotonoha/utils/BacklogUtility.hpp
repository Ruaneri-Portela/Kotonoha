#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/Engine.hpp>

namespace Kotonoha {

inline bool ShouldOpenBacklog(Engine &engine, EventRef event) {
  if (engine.GetRuntime().GetState() != GameState::Gameplay)
    return false;

  // Key B
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_B) {
    return true;
  }

  // Mouse wheel scroll up
  if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y > 0) {
    return true;
  }

  return false;
}

} // namespace Kotonoha
