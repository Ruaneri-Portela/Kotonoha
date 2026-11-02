#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <algorithm>
#include <functional>
#include <vector>

namespace Kotonoha {
// Input/state for native sprite controls. Ports provide their own atlas and
// optional pixel hitmap; mouse and synthesized touch use the same path.
class ImageButtonBar {
public:
  struct Button {
    SDL_FRect area;
    std::function<void()> action;
  };
  std::vector<Button> buttons;
  std::function<int(SDL_FPoint)> hitTest;
  int hovered = -1, pressed = -1;
  int hit(SDL_FPoint point) const {
    if (hitTest)
      return hitTest(point);
    for (std::size_t i = 0; i < buttons.size(); ++i)
      if (SDL_PointInRectFloat(&point, &buttons[i].area))
        return int(i);
    return -1;
  }
  void reset() { hovered = pressed = -1; }
  int state(int index) const {
    return pressed == index && hovered == index ? 2 : hovered == index ? 1 : 0;
  }
  int touchHit(SDL_FPoint point) const {
    const int exact = hit(point);
    if (exact >= 0)
      return exact;
    int closest = -1;
    float distance = 1e30f;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
      auto area = buttons[i].area;
      const float xpad = std::max(0.0f, (28 - area.w) / 2);
      const float ypad = std::max(0.0f, (28 - area.h) / 2);
      area = {area.x - xpad, area.y - ypad, area.w + xpad * 2,
              area.h + ypad * 2};
      const float x = point.x - area.x - area.w / 2,
                  y = point.y - area.y - area.h / 2;
      const float next = x * x + y * y;
      if (SDL_PointInRectFloat(&point, &area) && next < distance) {
        closest = int(i);
        distance = next;
      }
    }
    return closest;
  }
  bool handle(EventRef event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      reset();
      return false;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      hovered = hit({event.motion.x, event.motion.y});
      return pressed >= 0;
    }
    if ((event.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
         event.type != SDL_EVENT_MOUSE_BUTTON_UP) ||
        event.button.button != SDL_BUTTON_LEFT)
      return false;
    hovered = event.button.which == SDL_TOUCH_MOUSEID
                  ? touchHit({event.button.x, event.button.y})
                  : hit({event.button.x, event.button.y});
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      pressed = hovered;
      return pressed >= 0;
    }
    const int previous = pressed;
    const bool consumed = previous >= 0 || hovered >= 0;
    pressed = -1;
    if (previous >= 0 && previous == hovered &&
        previous < int(buttons.size())) {
      const auto action = buttons[previous].action;
      if (action)
        action();
    }
    if (event.button.which == SDL_TOUCH_MOUSEID)
      hovered = -1;
    return consumed;
  }
};
} // namespace Kotonoha
