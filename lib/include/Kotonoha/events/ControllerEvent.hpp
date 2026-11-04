#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <map>
#include <vector>

namespace Kotonoha {

// The engine's default layout treats the east face button as confirm and the
// south face button as cancel. Nintendo-style pads invert that expectation, so
// the settings UI exposes an explicit swap while a gamepad is connected.
inline SDL_GamepadButton SwapControllerAB(SDL_GamepadButton button) {
  if (button == SDL_GAMEPAD_BUTTON_SOUTH)
    return SDL_GAMEPAD_BUTTON_EAST;
  if (button == SDL_GAMEPAD_BUTTON_EAST)
    return SDL_GAMEPAD_BUTTON_SOUTH;
  return button;
}

// Normalizes a raw gamepad button event before any component maps it. Applying
// the swap here keeps every menu, dialogue and game-specific shortcut on the
// same physical button without duplicating the preference in each handler.
inline SDL_Event NormalizeControllerEvent(EventRef event, bool swapAB) {
  if (!swapAB || (event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                  event.type != SDL_EVENT_GAMEPAD_BUTTON_UP))
    return event;
  SDL_Event normalized = event;
  normalized.gbutton.button =
      SwapControllerAB(static_cast<SDL_GamepadButton>(event.gbutton.button));
  return normalized;
}

inline SDL_Event MapControllerEvent(EventRef event, bool dialogue = false,
                                    bool history = false) {
  if (event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
      event.type != SDL_EVENT_GAMEPAD_BUTTON_UP)
    return event;

  SDL_Keycode key = SDLK_UNKNOWN;
  switch (event.gbutton.button) {
  case SDL_GAMEPAD_BUTTON_EAST:
    key = SDLK_RETURN; // Confirm / Advance
    break;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    key = dialogue ? SDLK_LCTRL : SDLK_ESCAPE; // Hold skip or Cancel/Back
    break;
  case SDL_GAMEPAD_BUTTON_WEST:
    key = SDLK_S; // Toggle read-skip
    break;
  case SDL_GAMEPAD_BUTTON_NORTH:
  case SDL_GAMEPAD_BUTTON_START:
    key = SDLK_ESCAPE; // Pause menu / Back
    break;
  case SDL_GAMEPAD_BUTTON_BACK:
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    key = history ? SDLK_ESCAPE : SDLK_PAGEUP; // History / Backlog
    break;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    key = SDLK_A; // Toggle Auto mode
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    key = dialogue ? SDLK_MINUS : SDLK_LEFT;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    key = dialogue ? SDLK_PLUS : SDLK_RIGHT;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    key = SDLK_UP;
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    key = SDLK_DOWN;
    break;
  default:
    return event;
  }

  SDL_Event mapped{};
  mapped.type = (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
                    ? SDL_EVENT_KEY_DOWN
                    : SDL_EVENT_KEY_UP;
  mapped.key.timestamp = event.gbutton.timestamp;
  mapped.key.key = key;
  mapped.key.down = (mapped.type == SDL_EVENT_KEY_DOWN);
  return mapped;
}

// Convert the left stick into the same directional buttons used by menus.
// Hysteresis prevents jitter around the dead zone from moving focus repeatedly.
class ControllerNavigation {
  std::map<std::pair<SDL_JoystickID, Uint8>, int> directions;

public:
  std::vector<SDL_Event> process(EventRef event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      directions.clear();
      return {};
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
      for (auto it = directions.begin(); it != directions.end();)
        if (it->first.first == event.gdevice.which)
          it = directions.erase(it);
        else
          ++it;
      return {};
    }
    if (event.type != SDL_EVENT_GAMEPAD_AXIS_MOTION ||
        (event.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX &&
         event.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY))
      return {};
    auto &previous = directions[{event.gaxis.which, event.gaxis.axis}];
    const int value = event.gaxis.value;
    const int direction = value <= -16000                     ? -1
                          : value >= 16000                    ? 1
                          : (value > -10000 && value < 10000) ? 0
                                                              : previous;
    if (direction == previous)
      return {};
    std::vector<SDL_Event> result;
    auto append = [&](int sign, bool down) {
      SDL_Event button{};
      button.type =
          down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
      button.gbutton.timestamp = event.gaxis.timestamp;
      button.gbutton.which = event.gaxis.which;
      button.gbutton.down = down;
      button.gbutton.button = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX
                                  ? (sign < 0 ? SDL_GAMEPAD_BUTTON_DPAD_LEFT
                                              : SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                                  : (sign < 0 ? SDL_GAMEPAD_BUTTON_DPAD_UP
                                              : SDL_GAMEPAD_BUTTON_DPAD_DOWN);
      result.push_back(button);
    };
    if (previous)
      append(previous, false);
    if (direction)
      append(direction, true);
    previous = direction;
    return result;
  }
};

} // namespace Kotonoha
