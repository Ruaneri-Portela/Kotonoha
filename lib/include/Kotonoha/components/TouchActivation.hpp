#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <cmath>
#include <map>
#include <utility>

namespace Kotonoha {
// A touch scroll, canceled contact or second finger must never activate a
// button when SDL/the engine subsequently delivers its synthetic mouse release.
class TouchActivation {
  using Contact = std::pair<SDL_TouchID, SDL_FingerID>;
  std::map<Contact, SDL_FPoint> contacts;
  bool observed = false, canceled = false;

public:
  void reset() {
    contacts.clear();
    observed = canceled = false;
  }
  bool accept(EventRef event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      contacts.clear();
      canceled = observed = true;
    }
    if (event.type >= SDL_EVENT_FINGER_DOWN &&
        event.type <= SDL_EVENT_FINGER_CANCELED) {
      const Contact id{event.tfinger.touchID, event.tfinger.fingerID};
      if (event.type == SDL_EVENT_FINGER_DOWN) {
        if (contacts.empty())
          canceled = false;
        observed = true;
        contacts[id] = {event.tfinger.x, event.tfinger.y};
        canceled |= contacts.size() > 1;
      } else {
        const auto found = contacts.find(id);
        if (found != contacts.end())
          canceled |= std::hypot(event.tfinger.x - found->second.x,
                                 event.tfinger.y - found->second.y) > .025f;
        if (event.type == SDL_EVENT_FINGER_UP ||
            event.type == SDL_EVENT_FINGER_CANCELED)
          contacts.erase(id);
        canceled |= event.type == SDL_EVENT_FINGER_CANCELED;
      }
    }
    return !(observed && canceled && event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
             event.button.which == SDL_TOUCH_MOUSEID);
  }
};
} // namespace Kotonoha
