// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <Kotonoha/components/TouchActivation.hpp>
#include <SDL3/SDL.h>
#include <set>
#include <utility>

namespace RealLive {
// Advances initiated by read-skip must not disable the read-skip toggle.
// Auto advance preserves auto mode; manual input cancels both modes.
inline void updateAdvanceModes(bool manual, bool preserveReadSkip,
                               bool &autoMode, bool &skipMode) {
  if (manual) {
    autoMode = false;
    skipMode = false;
  } else if (!preserveReadSkip) {
    skipMode = false;
  }
}

// Track Ctrl, gamepad South and each physical trigger independently.
class RealLiveInput {
  Kotonoha::TouchActivation touches_;
  bool leftCtrl_ = false, rightCtrl_ = false;
  std::set<SDL_JoystickID> south_;
  std::set<std::pair<SDL_JoystickID, Uint8>> triggers_;

public:
  bool acceptTouch(const SDL_Event &e) { return touches_.accept(e); }
  bool skipHeld() const {
    return leftCtrl_ || rightCtrl_ || !south_.empty() || !triggers_.empty();
  }
  void resetSkip() {
    leftCtrl_ = rightCtrl_ = false;
    south_.clear();
    triggers_.clear();
  }
  bool processSkip(const SDL_Event &e, bool dialogue) {
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      resetSkip();
      return false;
    }
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
      south_.erase(e.gdevice.which);
      for (auto it = triggers_.begin(); it != triggers_.end();)
        if (it->first == e.gdevice.which)
          it = triggers_.erase(it);
        else
          ++it;
      return false;
    }
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
      if (e.key.key == SDLK_LCTRL) {
        leftCtrl_ = e.type == SDL_EVENT_KEY_DOWN;
        return true;
      }
      if (e.key.key == SDLK_RCTRL) {
        rightCtrl_ = e.type == SDL_EVENT_KEY_DOWN;
        return true;
      }
    }
    if ((e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
         e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
        e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
      if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && dialogue)
        south_.insert(e.gbutton.which);
      else
        south_.erase(e.gbutton.which);
      return dialogue; // Outside dialogue, South remains the menu cancel key.
    }
    if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
         e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
      const auto key = std::make_pair(e.gaxis.which, Uint8(e.gaxis.axis));
      if (dialogue && e.gaxis.value > 4000)
        triggers_.insert(key);
      else
        triggers_.erase(key);
      return true;
    }
    return false;
  }
};
} // namespace RealLive
