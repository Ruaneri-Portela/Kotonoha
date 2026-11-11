// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <SDL3/SDL.h>
#include <vector>

namespace RealLive {

// Virtual pointer for the native REALLIVE title screen. The title script never
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
  static constexpr int kTargetCount = 16;
  struct Target {
    int x, y;
  };
  static Target target(int index) {
    static constexpr Target targets[6] = {
        {320, 296}, {320, 324}, {320, 352}, {320, 380}, {320, 408}, {591, 463},
    };
    return targets[index];
  }
  // The title scripts of different RealLive releases place their menu entries
  // at different coordinates. The runtime records the native hit boxes while
  // the script polls them and installs their centres here.
  void setTargets(std::vector<Target> targets) {
    if (targets.empty())
      return;
    targets_ = std::move(targets);
    focus(focus_ < 0 ? 0 : focus_);
  }
  // Places the virtual pointer on the first entry as soon as the title is
  // ready. The native script records the hovered entry on a loop before the
  // click, so a press without a prior hover would not activate anything.
  void ensureFocus() {
    if (targets_.empty())
      return;
    focus(focus_ < 0 ? 0 : focus_);
  }
  std::size_t targetCount() const { return targets_.size(); }

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

  // Records a real pointer press/release on the title screen. The script
  // clears the native button register at the start of every poll loop, so a
  // queued SDL mouse event can otherwise be lost before 1:4:133 samples it.
  // Keeping press and release separate guarantees both frames are seen even
  // when a click is delivered between two polls.
  void pointer(bool down, int x, int y) {
    x_ = x;
    y_ = y;
    if (down)
      pointerPress_ = true;
    else
      pointerRelease_ = true;
  }
  void hover(int x, int y) {
    x_ = x;
    y_ = y;
  }

  // Returns the value the script should read for the polled button register on
  // this 1:4:133 poll: 1 on the press frame, 2 on the release frame, or -1 to
  // leave the real mouse button register untouched. Called once per poll.
  int poll() {
    if (pointerPress_ || pending_ == 1) {
      const bool keyboard = pending_ == 1;
      pointerPress_ = false;
      pending_ = 0;
      pointerHeld_ = true;
      // The title state machine records the hovered entry on the first loop
      // after the press. Release on the immediately following poll so the
      // script sees press (1) then release (2) on consecutive polls.
      holdPolls_ = 0;
      keyboardRelease_ = keyboard;
      return 1;
    }
    if (pointerHeld_ && (pointerRelease_ || keyboardRelease_)) {
      if (holdPolls_ > 0) {
        --holdPolls_;
        return -1;
      }
      pointerRelease_ = keyboardRelease_ = false;
      pointerHeld_ = false;
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
    pointerPress_ = pointerRelease_ = pointerHeld_ = keyboardRelease_ = false;
    holdPolls_ = 0;
  }

private:
  void focus(int index) {
    if (targets_.empty())
      return;
    const int n = int(targets_.size());
    focus_ = ((index % n) + n) % n;
    const auto point = targets_[focus_];
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
    focus(focus_ + delta);
  }

  int focus_ = -1;
  int x_ = -1, y_ = -1;
  int pending_ = 0; // 0 idle, 1 press pending, 2 release pending
  bool pointerPress_ = false, pointerRelease_ = false, pointerHeld_ = false,
       keyboardRelease_ = false;
  int holdPolls_ = 0;
  std::vector<Target> targets_ = [] {
    std::vector<Target> targets;
    for (int i = 0; i < 6; ++i)
      targets.push_back(target(i));
    return targets;
  }();
};

} // namespace RealLive
