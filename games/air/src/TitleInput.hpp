// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <SDL3/SDL.h>

namespace Air {

// Virtual pointer for the native AIR title screen. The title script never
// reads keyboard state: it polls the mouse position and the left/right button
// registers with 1:4:133 and activates on the release frame. Keyboard and
// gamepad input therefore move a virtual pointer over the script's native
// hitboxes and inject a two-poll press/release instead of faking an SDL mouse
// device.
class TitleInput {
public:
  // Native hitboxes from SEEN 9032: five main entries in a column at
  // x 220..420 plus the exit entry at x 552..630. A point safely inside each
  // box is enough; the script compares the polled integer coordinates.
  static constexpr int kTargetCount = 6;
  struct Target {
    int x, y;
  };
  static Target target(int index) {
    static constexpr Target targets[kTargetCount] = {
        {320, 296}, {320, 324}, {320, 352}, {320, 380}, {320, 408}, {591, 463},
    };
    return targets[index];
  }

  // Handles a keyboard event already mapped from a gamepad. Returns true when
  // the event belongs to the virtual cursor. Pointer and touch events release
  // the virtual cursor so native hover/click keeps working.
  bool handle(const SDL_Event &event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_FINGER_DOWN ||
        event.type == SDL_EVENT_FINGER_UP ||
        event.type == SDL_EVENT_FINGER_MOTION) {
      reset();
      return false;
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
      return false;
    switch (event.key.key) {
    case SDLK_UP:
    case SDLK_LEFT:
      move(-1);
      return true;
    case SDLK_DOWN:
    case SDLK_RIGHT:
      move(1);
      return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
      if (focus_ < 0)
        focus(0);
      if (pending_ == 0)
        pending_ = 1;
      return true;
    default:
      return false;
    }
  }

  // Returns the value the script should read for the polled button register on
  // this 1:4:133 poll: 1 on the press frame, 2 on the release frame, or -1 to
  // leave the real mouse button register untouched. Called once per poll.
  int poll() {
    if (pending_ == 1) {
      pending_ = 2;
      return 1;
    }
    if (pending_ == 2) {
      pending_ = 0;
      return 2;
    }
    return -1;
  }

  bool active() const { return focus_ >= 0; }
  int focus() const { return focus_; }
  int mouseX() const { return x_; }
  int mouseY() const { return y_; }

  void reset() {
    focus_ = -1;
    x_ = y_ = -1;
    pending_ = 0;
  }

private:
  void focus(int index) {
    focus_ = index;
    const auto point = target(index);
    x_ = point.x;
    y_ = point.y;
  }
  void move(int delta) {
    // The first directional press selects the first entry so Up/Down at boot
    // never lands on Exit by accident.
    if (focus_ < 0) {
      focus(0);
      return;
    }
    focus((focus_ + delta + kTargetCount) % kTargetCount);
  }

  int focus_ = -1;
  int x_ = -1, y_ = -1;
  int pending_ = 0; // 0 idle, 1 press pending, 2 release pending
};

} // namespace Air
