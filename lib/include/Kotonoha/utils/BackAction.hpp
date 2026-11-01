#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <unordered_set>

namespace Kotonoha {

inline bool BackAction(Engine &engine, EventRef event) {
  // 1. ESC or AC_BACK key down
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK) {
      return true;
    }
  }

  // 2. Mouse right click
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
    return true;
  }

  // 3. Two-finger tap or edge swipe
  static std::unordered_set<SDL_FingerID> backActiveFingers;
  static float backEdgeSwipeStartX = -1;
  static bool backEdgeSwipeActive = false;

  const SDL_Point view = engine.GetRuntime().viewport();

  if (event.type == SDL_EVENT_FINGER_DOWN) {
    backActiveFingers.insert(event.tfinger.fingerID);
    if (backActiveFingers.size() >= 2) {
      backActiveFingers.clear();
      return true;
    }
    if (event.tfinger.x * view.x < 40) {
      backEdgeSwipeStartX = static_cast<float>(event.tfinger.x * view.x);
      backEdgeSwipeActive = true;
    }
  } else if (event.type == SDL_EVENT_FINGER_MOTION && backEdgeSwipeActive) {
    if (event.tfinger.x * view.x > backEdgeSwipeStartX + 60) {
      backEdgeSwipeActive = false;
      backEdgeSwipeStartX = -1;
      return true;
    }
  } else if (event.type == SDL_EVENT_FINGER_UP) {
    backActiveFingers.erase(event.tfinger.fingerID);
    backEdgeSwipeActive = false;
    backEdgeSwipeStartX = -1;
  }

  // Mouse edge swipe
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (event.button.x < 40) {
      backEdgeSwipeStartX = static_cast<float>(event.button.x);
      backEdgeSwipeActive = true;
    }
  } else if (event.type == SDL_EVENT_MOUSE_MOTION && backEdgeSwipeActive) {
    if (event.motion.x > backEdgeSwipeStartX + 60) {
      backEdgeSwipeActive = false;
      backEdgeSwipeStartX = -1;
      return true;
    }
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    backEdgeSwipeActive = false;
    backEdgeSwipeStartX = -1;
  }

  return false;
}

} // namespace Kotonoha
