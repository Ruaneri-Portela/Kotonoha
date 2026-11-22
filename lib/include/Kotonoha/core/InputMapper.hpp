#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Kotonoha {

enum class InputAction {
  None,
  Confirm,
  Cancel,
  Menu,
  Backlog,
  AutoToggle,
  SkipHold,
  SkipToggle,
  QuickSave,
  QuickLoad,
  NavUp,
  NavDown,
  NavLeft,
  NavRight,
  PageUp,
  PageDown,
};

enum class InputContext {
  Menu,
  Dialogue,
  Backlog,
};

struct MappedInput {
  InputAction action = InputAction::None;
  bool pressed = false;
  bool repeat = false;
  bool hasPointer = false;
  SDL_FPoint pointer{0.0f, 0.0f};
};

// Manages SDL3 gamepad hotplug using RAII GamepadPtr handles.
class GamepadManager {
  std::unordered_map<SDL_JoystickID, GamepadPtr> gamepads_;
  void open(SDL_JoystickID id);

public:
  void openConnected();
  bool handleEvent(EventRef event);
  std::size_t count() const noexcept { return gamepads_.size(); }
  void clear() noexcept { gamepads_.clear(); }
};

// Generates key-repeat events for held D-pad / virtual-stick directions.
class NavRepeater {
  struct HeldDirection {
    SDL_JoystickID which = 0;
    Uint8 button = 0;
    Uint64 nextRepeatMs = 0;
  };
  std::vector<HeldDirection> held_;
  Uint64 initialDelayMs_ = 380;
  Uint64 repeatIntervalMs_ = 95;

  static bool isDpadButton(Uint8 button) noexcept;

public:
  NavRepeater() = default;
  NavRepeater(Uint64 initialDelayMs, Uint64 repeatIntervalMs)
      : initialDelayMs_(initialDelayMs), repeatIntervalMs_(repeatIntervalMs) {}

  void process(EventRef event, Uint64 nowMs);
  std::vector<SDL_Event> update(Uint64 nowMs);
  void reset();
  bool hasHeld() const noexcept { return !held_.empty(); }
};

// Shared cross-device input mapper (Mouse, Touch, Keyboard, Gamepad).
class InputMapper {
  ControllerNavigation stickNav_;
  NavRepeater navRepeater_;

public:
  InputMapper() = default;
  InputMapper(Uint64 initialRepeatDelayMs, Uint64 repeatIntervalMs)
      : navRepeater_(initialRepeatDelayMs, repeatIntervalMs) {}

  static SDL_FPoint touchToLogical(float normX, float normY, int logicalW,
                                   int logicalH) noexcept;
  static SDL_FPoint touchToLogical(const SDL_TouchFingerEvent &finger,
                                   SDL_Point viewport) noexcept;

  static bool isConfirmKey(SDL_Keycode key) noexcept;
  static bool isBackKey(SDL_Keycode key) noexcept;
  static bool isBacklogKey(SDL_Keycode key) noexcept;
  static bool isSkipHoldKey(SDL_Keycode key) noexcept;

  static SDL_Event toKeyEvent(EventRef event,
                              InputContext context = InputContext::Menu,
                              bool swapAB = false);
  static MappedInput mapEvent(EventRef event,
                              InputContext context = InputContext::Menu,
                              bool swapAB = false);

  // Processes stick-to-dpad conversion and tracks directional repeat state.
  std::vector<SDL_Event> processDirectional(EventRef event, Uint64 nowMs);
  std::vector<SDL_Event> updateRepeat(Uint64 nowMs);
  void reset();
};

} // namespace Kotonoha
