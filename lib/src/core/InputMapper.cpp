#include <Kotonoha/core/InputMapper.hpp>
#include <algorithm>

namespace Kotonoha {

void GamepadManager::open(SDL_JoystickID id) {
  if (gamepads_.count(id))
    return;
  GamepadPtr pad{SDL_OpenGamepad(id)};
  if (pad)
    gamepads_.emplace(id, std::move(pad));
  else
    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cannot open gamepad: %s",
                SDL_GetError());
}

void GamepadManager::openConnected() {
  int count = 0;
  SDLArrayPtr<SDL_JoystickID> ids{SDL_GetGamepads(&count)};
  for (int i = 0; ids && i < count; ++i)
    open(ids.get()[i]);
}

bool GamepadManager::handleEvent(EventRef event) {
  if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
    open(event.gdevice.which);
    return true;
  }
  if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
    gamepads_.erase(event.gdevice.which);
    return true;
  }
  if (event.type == SDL_EVENT_JOYSTICK_ADDED &&
      SDL_IsGamepad(event.jdevice.which)) {
    open(event.jdevice.which);
    return true;
  }
  if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
    gamepads_.erase(event.jdevice.which);
    return true;
  }
  return false;
}

bool NavRepeater::isDpadButton(Uint8 button) noexcept {
  return button == SDL_GAMEPAD_BUTTON_DPAD_UP ||
         button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
         button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
         button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
}

void NavRepeater::process(EventRef event, Uint64 nowMs) {
  if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    held_.clear();
    return;
  }
  if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
    held_.erase(std::remove_if(held_.begin(), held_.end(),
                               [&](const HeldDirection &d) {
                                 return d.which == event.gdevice.which;
                               }),
                held_.end());
    return;
  }
  if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
      isDpadButton(event.gbutton.button)) {
    for (auto &d : held_) {
      if (d.which == event.gbutton.which && d.button == event.gbutton.button)
        return;
    }
    held_.push_back(
        {event.gbutton.which, event.gbutton.button, nowMs + initialDelayMs_});
    return;
  }
  if (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP &&
      isDpadButton(event.gbutton.button)) {
    held_.erase(std::remove_if(held_.begin(), held_.end(),
                               [&](const HeldDirection &d) {
                                 return d.which == event.gbutton.which &&
                                        d.button == event.gbutton.button;
                               }),
                held_.end());
  }
}

std::vector<SDL_Event> NavRepeater::update(Uint64 nowMs) {
  std::vector<SDL_Event> emitted;
  for (auto &d : held_) {
    if (nowMs >= d.nextRepeatMs) {
      SDL_Event ev{};
      ev.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
      ev.gbutton.timestamp = nowMs * 1000000ULL;
      ev.gbutton.which = d.which;
      ev.gbutton.button = d.button;
      ev.gbutton.down = true;
      emitted.push_back(ev);
      d.nextRepeatMs = nowMs + repeatIntervalMs_;
    }
  }
  return emitted;
}

void NavRepeater::reset() { held_.clear(); }

SDL_FPoint InputMapper::touchToLogical(float normX, float normY, int logicalW,
                                       int logicalH) noexcept {
  const float x = Clamp(normX, 0.0f, 1.0f);
  const float y = Clamp(normY, 0.0f, 1.0f);
  return {x * static_cast<float>(std::max(0, logicalW)),
          y * static_cast<float>(std::max(0, logicalH))};
}

SDL_FPoint InputMapper::touchToLogical(const SDL_TouchFingerEvent &finger,
                                       SDL_Point viewport) noexcept {
  return touchToLogical(finger.x, finger.y, viewport.x, viewport.y);
}

bool InputMapper::isConfirmKey(SDL_Keycode key) noexcept {
  return key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_KP_ENTER;
}

bool InputMapper::isBackKey(SDL_Keycode key) noexcept {
  return key == SDLK_ESCAPE || key == SDLK_AC_BACK || key == SDLK_BACKSPACE;
}

bool InputMapper::isBacklogKey(SDL_Keycode key) noexcept {
  return key == SDLK_PAGEUP || key == SDLK_B;
}

bool InputMapper::isSkipHoldKey(SDL_Keycode key) noexcept {
  return key == SDLK_LCTRL || key == SDLK_RCTRL;
}

SDL_Event InputMapper::toKeyEvent(EventRef event, InputContext context,
                                  bool swapAB) {
  const SDL_Event normalized = NormalizeControllerEvent(event, swapAB);
  const bool dialogue = context == InputContext::Dialogue;
  const bool history = context == InputContext::Backlog;
  return MapControllerEvent(normalized, dialogue, history);
}

MappedInput InputMapper::mapEvent(EventRef event, InputContext context,
                                  bool swapAB) {
  MappedInput result{};
  const SDL_Event normalized = NormalizeControllerEvent(event, swapAB);

  if (normalized.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      normalized.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    result.pressed = normalized.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    result.hasPointer = true;
    result.pointer = {normalized.button.x, normalized.button.y};
    if (normalized.button.button == SDL_BUTTON_LEFT)
      result.action = InputAction::Confirm;
    else if (normalized.button.button == SDL_BUTTON_RIGHT)
      result.action = InputAction::Cancel;
    return result;
  }

  if (normalized.type == SDL_EVENT_MOUSE_WHEEL) {
    const float dir =
        normalized.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
    const float wy = normalized.wheel.y * dir;
    if (wy > 0.0f) {
      result.action = context == InputContext::Backlog ? InputAction::PageUp
                                                       : InputAction::Backlog;
      result.pressed = true;
    } else if (wy < 0.0f) {
      result.action = InputAction::PageDown;
      result.pressed = true;
    }
    return result;
  }

  const SDL_Event mapped = toKeyEvent(normalized, context, false);
  if (mapped.type == SDL_EVENT_KEY_DOWN || mapped.type == SDL_EVENT_KEY_UP) {
    result.pressed = mapped.type == SDL_EVENT_KEY_DOWN;
    result.repeat = mapped.key.repeat;
    switch (mapped.key.key) {
    case SDLK_RETURN:
    case SDLK_SPACE:
    case SDLK_KP_ENTER:
      result.action = InputAction::Confirm;
      break;
    case SDLK_ESCAPE:
      result.action = context == InputContext::Dialogue ? InputAction::Menu
                                                        : InputAction::Cancel;
      break;
    case SDLK_AC_BACK:
    case SDLK_BACKSPACE:
      result.action = InputAction::Cancel;
      break;
    case SDLK_UP:
      result.action = InputAction::NavUp;
      break;
    case SDLK_DOWN:
      result.action = InputAction::NavDown;
      break;
    case SDLK_LEFT:
      result.action = InputAction::NavLeft;
      break;
    case SDLK_RIGHT:
      result.action = InputAction::NavRight;
      break;
    case SDLK_PAGEUP:
      result.action = context == InputContext::Backlog ? InputAction::PageUp
                                                       : InputAction::Backlog;
      break;
    case SDLK_PAGEDOWN:
      result.action = InputAction::PageDown;
      break;
    case SDLK_B:
      result.action = InputAction::Backlog;
      break;
    case SDLK_A:
      result.action = InputAction::AutoToggle;
      break;
    case SDLK_S:
      result.action = InputAction::SkipToggle;
      break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      result.action = InputAction::SkipHold;
      break;
    case SDLK_F5:
      result.action = InputAction::QuickSave;
      break;
    case SDLK_F6:
    case SDLK_F9:
      result.action = InputAction::QuickLoad;
      break;
    default:
      break;
    }
  }
  return result;
}

std::vector<SDL_Event> InputMapper::processDirectional(EventRef event,
                                                       Uint64 nowMs) {
  auto generated = stickNav_.process(event);
  for (const auto &ev : generated)
    navRepeater_.process(ev, nowMs);
  if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
      event.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
      event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
      event.type == SDL_EVENT_GAMEPAD_REMOVED) {
    navRepeater_.process(event, nowMs);
  }
  return generated;
}

std::vector<SDL_Event> InputMapper::updateRepeat(Uint64 nowMs) {
  return navRepeater_.update(nowMs);
}

void InputMapper::reset() { navRepeater_.reset(); }

} // namespace Kotonoha
