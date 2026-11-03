#pragma once
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/InputActions.hpp>
#include <algorithm>
#include <cmath>
#include <functional>

namespace Kotonoha {
// Modal, scrollable selection shared by native engine settings.
class SelectBox {
  std::vector<std::string> options;
  std::function<void(int)> changed;
  std::string title;
  int selected = 0;
  float scroll = 0, velocity = 0, pressX = 0, pressY = 0;
  bool follow = false, visible = false, dragged = false;
  ScrollGesture gesture;

public:
  bool isOpen() const { return visible; }
  void close() {
    visible = false;
    gesture.reset();
  }
  void open(std::string caption, std::vector<std::string> items, int current,
            std::function<void(int)> action) {
    title = std::move(caption);
    options = std::move(items);
    selected = std::clamp(current, 0, std::max(0, int(options.size()) - 1));
    scroll = float(std::max(0, selected - 5) * 44);
    changed = std::move(action);
    visible = true;
    dragged = false;
  }
  void onEvent(EventRef event) {
    if (!visible)
      return;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK)
        return close();
      if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN) {
        selected = std::clamp(selected + (event.key.key == SDLK_UP ? -1 : 1), 0,
                              std::max(0, int(options.size()) - 1));
        scroll = float(std::max(0, selected - 5) * 44);
      } else if (event.key.key == SDLK_RETURN && !options.empty()) {
        auto action = changed;
        close();
        action(selected);
        return;
      }
    }
    if (event.type == SDL_EVENT_FINGER_DOWN) {
      dragged = false;
      pressX = event.tfinger.x;
      pressY = event.tfinger.y;
    }
    if (event.type == SDL_EVENT_FINGER_MOTION &&
        std::hypot(event.tfinger.x - pressX, event.tfinger.y - pressY) > .025f)
      dragged = true;
    gesture.handle(event, 720, scroll, velocity, follow,
                   std::max(0.f, float(options.size()) * 44 - 440));
    if (UI::clicked(event, {350, 620, 580, 44}))
      return close();
    if (dragged && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
      return;
    for (int i = 0; i < int(options.size()); ++i) {
      const float y = 150 + i * 44 - scroll;
      if (y < 150 || y + 42 > 590)
        continue;
      if (UI::clicked(event, {350, y, 580, 42})) {
        auto action = changed;
        close();
        action(i);
        return;
      }
    }
  }
  void render(RendererRef renderer, UI &ui) {
    if (!visible)
      return;
    ui.backdrop(renderer, {0, 0, 0, 220});
    ui.panel(renderer, {325, 65, 630, 620});
    ui.text(renderer, title, 350, 90, 580, {255, 215, 100, 255});
    SDL_Rect clip{350, 150, 580, 440};
    SDL_SetRenderClipRect(&renderer, &clip);
    for (int i = 0; i < int(options.size()); ++i) {
      const float y = 150 + i * 44 - scroll;
      if (y + 42 >= 150 && y < 590)
        ui.button(renderer, {350, y, 580, 42}, options[i], selected == i);
    }
    SDL_SetRenderClipRect(&renderer, nullptr);
    ui.button(renderer, {350, 620, 580, 44}, "Cancel");
  }
};
} // namespace Kotonoha
