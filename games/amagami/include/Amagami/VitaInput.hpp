#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <array>
#include <cstdint>

namespace Amagami::Vita {

// SDL event -> original PS2 physical button layout, shared by desktop/Vita
// diagnostics. VitaVmBridge::inputFrame converts these values to SceCtrl
// masks for system classes. Keeping keyboard/gamepad state separate avoids
// releasing a key when the same button is held on the other device.
class VitaInput {
public:
  void handle(const SDL_Event &event);
  uint32_t buttons() const { return keyboard_ | gamepad_; }
  const std::array<int, 4> &analog() const { return analog_; }
private:
  uint32_t keyboard_ = 0, gamepad_ = 0;
  SDL_JoystickID activeGamepad_ = 0;
  std::array<int, 4> analog_{{0, 0, 0, 0}};
};

} // namespace Amagami::Vita
