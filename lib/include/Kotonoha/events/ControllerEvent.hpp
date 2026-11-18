#pragma once

#include <algorithm>
#include <Kotonoha/core/SDL.hpp>
#include <map>
#include <vector>

namespace Kotonoha {

// C++17 compatible clamp function
template <typename T>
constexpr T Clamp(T value, T min, T max) {
  return value < min ? min : (value > max ? max : value);
}

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
  if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
      (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
       event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
    SDL_Event mapped{};
    mapped.type =
        (event.gaxis.value > 4000) ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    mapped.key.timestamp = event.gaxis.timestamp;
    mapped.key.key = SDLK_LCTRL;
    mapped.key.down = (mapped.type == SDL_EVENT_KEY_DOWN);
    return mapped;
  }

  if (event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
      event.type != SDL_EVENT_GAMEPAD_BUTTON_UP)
    return event;

  SDL_Keycode key = SDLK_UNKNOWN;
  switch (event.gbutton.button) {
  case SDL_GAMEPAD_BUTTON_EAST:
    key = SDLK_RETURN; // Confirm / Advance
    break;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    key = dialogue ? SDLK_LCTRL : SDLK_AC_BACK; // Hold skip or Voltar/Cancel
    break;
  case SDL_GAMEPAD_BUTTON_WEST:
    key = SDLK_S; // Toggle read-skip
    break;
  case SDL_GAMEPAD_BUTTON_NORTH:
    key = SDLK_AC_BACK; // Y button = Voltar (Back / Return)
    break;
  case SDL_GAMEPAD_BUTTON_START:
    key = SDLK_ESCAPE; // Dedicated Start button = Open InGameMenu
    break;
  case SDL_GAMEPAD_BUTTON_BACK:
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    key = history ? SDLK_ESCAPE : SDLK_PAGEUP; // Open or close history
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

// Convert left-stick axes to virtual D-pad events. Releasing a controller or
// losing focus also releases its held directions so no menu keeps a stuck key.
class ControllerNavigation {
  using Axis = std::pair<SDL_JoystickID, Uint8>;
  std::map<Axis, int> directions;

  static SDL_Event button(const Axis &axis, int sign, bool down,
                          Uint64 timestamp) {
    SDL_Event result{};
    result.type =
        down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    result.gbutton.timestamp = timestamp;
    result.gbutton.which = axis.first;
    result.gbutton.down = down;
    result.gbutton.button = axis.second == SDL_GAMEPAD_AXIS_LEFTX
                                ? (sign < 0 ? SDL_GAMEPAD_BUTTON_DPAD_LEFT
                                            : SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                                : (sign < 0 ? SDL_GAMEPAD_BUTTON_DPAD_UP
                                            : SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    return result;
  }

public:
  std::vector<SDL_Event> process(EventRef event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        event.type == SDL_EVENT_GAMEPAD_REMOVED) {
      std::vector<SDL_Event> released;
      for (auto it = directions.begin(); it != directions.end();) {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
            it->first.first == event.gdevice.which) {
          if (it->second)
            released.push_back(button(it->first, it->second, false,
                                      event.common.timestamp));
          it = directions.erase(it);
        } else {
          ++it;
        }
      }
      return released;
    }
    if (event.type != SDL_EVENT_GAMEPAD_AXIS_MOTION ||
        (event.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX &&
         event.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY))
      return {};

    const Axis axis{event.gaxis.which, event.gaxis.axis};
    auto &previous = directions[axis];
    const int value = event.gaxis.value;
    const int direction = value <= -16000                     ? -1
                          : value >= 16000                    ? 1
                          : (value > -10000 && value < 10000) ? 0
                                                              : previous;
    if (direction == previous)
      return {};
    std::vector<SDL_Event> result;
    if (previous)
      result.push_back(button(axis, previous, false, event.gaxis.timestamp));
    if (direction)
      result.push_back(button(axis, direction, true, event.gaxis.timestamp));
    previous = direction;
    return result;
  }
};

// Track both trigger axes of every connected gamepad separately. Releasing
// one trigger must not cancel skip while another trigger remains depressed.
class ControllerTriggerSkip {
  using Axis = std::pair<SDL_JoystickID, Uint8>;
  std::map<Axis, float> pressures_;

public:
  float pressure() const {
    float strongest = 0.0f;
    for (const auto &entry : pressures_)
      // Win32 consumers may have included the max(a,b) macro before this
      // header. Parenthesizing the function name prevents macro expansion.
      strongest = (std::max)(strongest, entry.second);
    return strongest;
  }
  bool active() const { return !pressures_.empty(); }

  bool process(EventRef event) {
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      pressures_.clear();
      return false;
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
      for (auto it = pressures_.begin(); it != pressures_.end();) {
        if (it->first.first == event.gdevice.which)
          it = pressures_.erase(it);
        else
          ++it;
      }
      return false;
    }
    if (event.type != SDL_EVENT_GAMEPAD_AXIS_MOTION ||
        (event.gaxis.axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
         event.gaxis.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
      return false;
    const Axis axis{event.gaxis.which, event.gaxis.axis};
    constexpr int kThreshold = 4000;
    if (event.gaxis.value > kThreshold) {
      pressures_[axis] =
          Clamp(static_cast<float>(event.gaxis.value - kThreshold) /
                    static_cast<float>(32767 - kThreshold),
                0.0f, 1.0f);
    } else {
      pressures_.erase(axis);
    }
    return true;
  }
};

} // namespace Kotonoha
