#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <array>
#include <cstdint>
#include <unordered_set>

namespace Amagami::Vita {

// SDL event -> original PS2 physical button layout, shared by desktop/Vita
// diagnostics. VitaVmBridge::inputFrame converts these values to SceCtrl
// masks for system classes. Keeping keyboard/gamepad state separate avoids
// releasing a key when the same button is held on the other device.
class VitaInput {
public:
  void handle(const SDL_Event &event);
  // Maps physical SDL buttons to game actions without a hardware handle.
  static uint32_t mapGamepadButton(uint8_t button, SDL_GamepadType type);
  uint32_t buttons() const { return keyboard_ | gamepad_ | stick_ | triggers_; }
  const std::array<int, 4> &analog() const { return analog_; }
private:
  // Track physical keys, not only action bits: Z and Space both confirm,
  // and releasing one must not release the other.
  std::unordered_set<SDL_Keycode> heldKeys_;
  uint32_t keyboard_ = 0, gamepad_ = 0;
  // Physical state survives live controller remapping without stuck buttons.
  uint32_t gamepadPhysical_ = 0;
  SDL_GamepadType gamepadType_ = SDL_GAMEPAD_TYPE_UNKNOWN;
  void updateGamepadButtons();
  // Keep analog and digital sources independent when a stick recentres.
  uint32_t stick_ = 0, triggers_ = 0;
  SDL_JoystickID activeGamepad_ = 0;
  std::array<int, 4> analog_{{0, 0, 0, 0}};
};

} // namespace Amagami::Vita
