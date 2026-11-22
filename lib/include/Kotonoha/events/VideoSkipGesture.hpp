#pragma once

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>

namespace Kotonoha {

// A video cannot be dismissed by incidental input (mouse motion, scroll,
// release events or a single tap). Keyboard and gamepad presses are explicit;
// mouse/touch pointers require two nearby presses within 350 ms.
// Keep one instance per active video and reset it when playback changes.
class VideoSkipGesture {
public:
  static constexpr Uint64 DoublePressIntervalMs = 350;

  // A looping menu/background movie is decorative: it must not intercept
  // choice input or be dismissed by a generic video-skip gesture.
  // Narrative cutscenes still capture input until skipped or finished.
  static constexpr bool capturesInput(bool active, bool looping) noexcept {
    return active && !looping;
  }

  void reset() {
    lastPressMs_ = 0;
    lastX_ = lastY_ = 0.0f;
    lastKind_ = 0;
    waiting_ = false;
  }

  bool accept(const SDL_Event &event, Uint64 nowMs = SDL_GetTicks()) {
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
      return true;
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
      return true;

    int kind = 0;
    float x = 0.0f, y = 0.0f;
    float threshold = 0.0f;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.which != SDL_TOUCH_MOUSEID &&
        event.button.button == SDL_BUTTON_LEFT) {
      if (event.button.clicks >= 2) {
        reset();
        return true;
      }
      kind = 1;
      x = event.button.x;
      y = event.button.y;
      threshold = 48.0f;  // SDL logical pixels
    } else if (event.type == SDL_EVENT_FINGER_DOWN) {
      kind = 2;
      x = event.tfinger.x;
      y = event.tfinger.y;
      threshold = 0.06f; // normalized touch coordinates
    } else {
      return false;
    }

    if (waiting_ && kind == lastKind_ && nowMs >= lastPressMs_ &&
        nowMs - lastPressMs_ <= DoublePressIntervalMs &&
        std::abs(x - lastX_) <= threshold &&
        std::abs(y - lastY_) <= threshold) {
      reset();
      return true;
    }

    waiting_ = true;
    lastKind_ = kind;
    lastPressMs_ = nowMs;
    lastX_ = x;
    lastY_ = y;
    return false;
  }

private:
  Uint64 lastPressMs_ = 0;
  float lastX_ = 0, lastY_ = 0;
  int lastKind_ = 0;
  bool waiting_ = false;
};

} // namespace Kotonoha
