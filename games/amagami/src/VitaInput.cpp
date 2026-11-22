#include <Amagami/VitaInput.hpp>
#include <algorithm>

namespace Amagami::Vita {
namespace {
uint32_t keyboardButton(SDL_Keycode key) {
  switch (key) {
  case SDLK_UP:
    return 4096;
  case SDLK_DOWN:
    return 16384;
  case SDLK_LEFT:
    return 32768;
  case SDLK_RIGHT:
    return 8192;
  case SDLK_Z:
  case SDLK_SPACE:
    return 32; // Circle: confirm.
  case SDLK_X:
    return 64; // Cross: cancel.
  case SDLK_A:
    return 128;
  case SDLK_S:
    return 16;
  case SDLK_Q:
    return 4; // L
  case SDLK_W:
    return 8; // R
  case SDLK_E:
    return 1; // L2
  case SDLK_R:
    return 2; // R2
  case SDLK_RETURN:
    return 2048; // Start
  case SDLK_BACKSPACE:
    return 256; // Select
  default:
    return 0;
  }
}
uint32_t controllerButton(uint8_t button, SDL_GamepadType type) {
  const bool xbox = type == SDL_GAMEPAD_TYPE_XBOX360 ||
                    type == SDL_GAMEPAD_TYPE_XBOXONE;
  switch (button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return 4096;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return 16384;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return 32768;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return 8192;
  case SDL_GAMEPAD_BUTTON_EAST:
    return xbox ? 64u : 32u;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    return xbox ? 32u : 64u;
  case SDL_GAMEPAD_BUTTON_WEST:
    return 128;
  case SDL_GAMEPAD_BUTTON_NORTH:
    return 16;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return 4;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return 8;
  case SDL_GAMEPAD_BUTTON_START:
    return 2048;
  case SDL_GAMEPAD_BUTTON_BACK:
    return 256;
  default:
    return 0;
  }
}
int axisIndex(uint8_t axis) {
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    return 0;
  case SDL_GAMEPAD_AXIS_LEFTY:
    return 1;
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return 2;
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return 3;
  default:
    return -1;
  }
}
} // namespace

uint32_t VitaInput::mapGamepadButton(uint8_t button, SDL_GamepadType type) {
  return controllerButton(button, type);
}
void VitaInput::updateGamepadButtons() {
  static_assert(SDL_GAMEPAD_BUTTON_COUNT <= 32,
                "Vita gamepad needs larger physical button storage");
  gamepad_ = 0;
  for (unsigned i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
    if (gamepadPhysical_ & (uint32_t{1} << i))
      gamepad_ |= mapGamepadButton(uint8_t(i), gamepadType_);
}

void VitaInput::handle(const SDL_Event &e) {
  if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    heldKeys_.clear();
    keyboard_ = 0; // Don't stick held keys when Alt-Tab releases them.
    return;
  }
  if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
    const uint32_t bit = keyboardButton(e.key.key);
    if (bit) {
      if (e.type == SDL_EVENT_KEY_DOWN)
        heldKeys_.insert(e.key.key);
      else
        heldKeys_.erase(e.key.key);
      keyboard_ = 0;
      for (SDL_Keycode held : heldKeys_)
        keyboard_ |= keyboardButton(held);
    }
    return;
  }
  if (e.type == SDL_EVENT_GAMEPAD_REMAPPED &&
      e.gdevice.which == activeGamepad_) {
    gamepadType_ = SDL_GetGamepadTypeForID(activeGamepad_);
    updateGamepadButtons();
    return;
  }
  if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
    if (e.gdevice.which == activeGamepad_) {
      activeGamepad_ = 0;
      gamepadPhysical_ = 0;
      gamepadType_ = SDL_GAMEPAD_TYPE_UNKNOWN;
      gamepad_ = 0;
      stick_ = triggers_ = 0;
      analog_ = {{0, 0, 0, 0}};
    }
    return;
  }
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
      e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
    if (!activeGamepad_ && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
      activeGamepad_ = e.gbutton.which;
      gamepadType_ = SDL_GetGamepadTypeForID(activeGamepad_);
    }
    if (activeGamepad_ != e.gbutton.which ||
        e.gbutton.button >= SDL_GAMEPAD_BUTTON_COUNT)
      return;
    const uint32_t bit = uint32_t{1} << e.gbutton.button;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
      gamepadPhysical_ |= bit;
    else
      gamepadPhysical_ &= ~bit;
    updateGamepadButtons();
    return;
  }
  if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
    if (!activeGamepad_) {
      activeGamepad_ = e.gaxis.which;
      gamepadType_ = SDL_GetGamepadTypeForID(activeGamepad_);
    }
    if (activeGamepad_ != e.gaxis.which)
      return;
    const int axis = axisIndex(e.gaxis.axis);
    if (axis >= 0) {
      // SceCtrl analog channels use signed 8-bit positions.
      analog_[std::size_t(axis)] =
          std::clamp(int(e.gaxis.value) / 256, -128, 127);
      if (axis < 2) {
        // open-amagami: the left stick also navigates as a D-pad, with a
        // deliberate 16000/32767 dead zone. Never erase a held D-pad button.
        const uint32_t negative = axis == 0 ? 32768u : 4096u;
        const uint32_t positive = axis == 0 ? 8192u : 16384u;
        stick_ &= ~(negative | positive);
        if (e.gaxis.value < -16000)
          stick_ |= negative;
        else if (e.gaxis.value > 16000)
          stick_ |= positive;
      }
    }
    if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
        e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
      const uint32_t bit =
          e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 1u : 2u;
      // Same threshold as the reference port's SDL trigger reader.
      if (e.gaxis.value > 4000)
        triggers_ |= bit;
      else
        triggers_ &= ~bit;
    }
  }
}
} // namespace Amagami::Vita
