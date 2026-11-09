#pragma once
#include <Kotonoha/componentes/TouchActivation.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
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
  TouchActivation touchActivation;

public:
  bool isOpen() const { return visible; }
  void close() {
    visible = false;
    gesture.reset();
  }
  void open(std::string caption, std::vector<std::string> items, int current,
            std::function<void(int)> action) {
    touchActivation.reset();
    title = std::move(caption);
    options = std::move(items);
    selected = std::clamp(current, 0, std::max(0, int(options.size()) - 1));
    scroll = std::clamp(float(std::max(0, selected - 5) * 44), 0.f,
                        std::max(0.f, float(options.size()) * 44 - 440));
    velocity = 0;
    follow = false;
    gesture.reset();
    changed = std::move(action);
    visible = true;
    dragged = false;
  }
  void onEvent(EventRef input) {
    if (!visible)
      return;
    const auto event = MapControllerEvent(input);
    if (!touchActivation.accept(event))
      return;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK)
        return close();
      const int maximum = std::max(0, int(options.size()) - 1);
      if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN ||
          event.key.key == SDLK_PAGEUP || event.key.key == SDLK_PAGEDOWN ||
          event.key.key == SDLK_HOME || event.key.key == SDLK_END) {
        if (event.key.key == SDLK_HOME)
          selected = 0;
        else if (event.key.key == SDLK_END)
          selected = maximum;
        else {
          const int step =
              (event.key.key == SDLK_PAGEUP || event.key.key == SDLK_PAGEDOWN)
                  ? 10
                  : 1;
          const bool reverse =
              event.key.key == SDLK_UP || event.key.key == SDLK_PAGEUP;
          selected =
              std::clamp(selected + (reverse ? -step : step), 0, maximum);
        }
        scroll = std::min(std::max(0.0f, float(int(options.size()) * 44 - 440)),
                          float(std::max(0, selected - 5) * 44));
        return;
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
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event.button.button == SDL_BUTTON_LEFT && event.button.x >= 936 &&
        event.button.x <= 953 && event.button.y >= 150 &&
        event.button.y <= 590) {
      // Clicking the visible scrollbar track jumps directly through long
      // lists, useful when a monitor exposes dozens of refresh-rate modes.
      const float maximum = std::max(0.f, float(options.size()) * 44 - 440);
      scroll = std::clamp((event.button.y - 150.f) / 440.f, 0.f, 1.f) * maximum;
      return;
    }
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
    const float maximum = std::max(0.f, float(options.size()) * 44 - 440);
    if (maximum > 0) {
      ui.panel(renderer, {938, 150, 9, 440}, {50, 60, 76, 255});
      const float thumb =
          std::max(24.f, 440.f * 440.f / (float(options.size()) * 44.f));
      ui.panel(renderer,
               {938, 150 + scroll / maximum * (440 - thumb), 9, thumb},
               {130, 190, 245, 255});
    }
    ui.text(renderer,
            std::to_string(selected + 1) + "/" +
                std::to_string(options.size()) +
                "  Wheel / swipe / PgUp / PgDn / Home / End",
            350, 593, 580, {215, 220, 235, 255});
    ui.button(renderer, {350, 620, 580, 44}, "Cancel");
  }
};
} // namespace Kotonoha
