#include <Kotonoha/core/InputMapper.hpp>
#include <Kotonoha/events/InputActions.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Kotonoha;

void check(bool ok, const char *msg) {
  if (!ok)
    throw std::runtime_error(msg);
}

SDL_Event keyEvent(SDL_Keycode key, bool down = true, bool repeat = false) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.key = key;
  e.key.down = down;
  e.key.repeat = repeat;
  return e;
}

SDL_Event padButton(SDL_GamepadButton btn, bool down = true,
                    SDL_JoystickID which = 1) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
  e.gbutton.which = which;
  e.gbutton.button = btn;
  e.gbutton.down = down;
  return e;
}

SDL_Event padAxis(SDL_GamepadAxis axis, Sint16 value,
                  SDL_JoystickID which = 1) {
  SDL_Event e{};
  e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  e.gaxis.which = which;
  e.gaxis.axis = axis;
  e.gaxis.value = value;
  return e;
}

SDL_Event fingerEvent(Uint32 type, float x, float y, SDL_FingerID id = 1) {
  SDL_Event e{};
  e.type = type;
  e.tfinger.touchID = 1;
  e.tfinger.fingerID = id;
  e.tfinger.x = x;
  e.tfinger.y = y;
  return e;
}

void testTouchCoordinateMapping() {
  const auto pt = InputMapper::touchToLogical(0.5f, 0.25f, 1280, 720);
  check(std::abs(pt.x - 640.0f) < 0.01f && std::abs(pt.y - 180.0f) < 0.01f,
        "Normalized touch maps to logical coordinates");
  const auto clamped = InputMapper::touchToLogical(-0.2f, 1.5f, 800, 600);
  check(clamped.x == 0.0f && clamped.y == 600.0f,
        "Out-of-range touch coordinates clamp cleanly");
  SDL_TouchFingerEvent tf{};
  tf.x = 0.75f;
  tf.y = 0.5f;
  const auto vp = InputMapper::touchToLogical(tf, {1920, 1080});
  check(std::abs(vp.x - 1440.0f) < 0.01f && std::abs(vp.y - 540.0f) < 0.01f,
        "TouchFingerEvent overload maps to viewport");
}

void testKeyboardAndMouseMapping() {
  check(InputMapper::mapEvent(keyEvent(SDLK_RETURN)).action ==
            InputAction::Confirm,
        "Enter maps to Confirm");
  check(InputMapper::mapEvent(keyEvent(SDLK_SPACE)).action ==
            InputAction::Confirm,
        "Space maps to Confirm");
  check(InputMapper::mapEvent(keyEvent(SDLK_ESCAPE), InputContext::Dialogue)
                .action == InputAction::Menu,
        "Escape maps to Menu in dialogue");
  check(
      InputMapper::mapEvent(keyEvent(SDLK_ESCAPE), InputContext::Menu).action ==
          InputAction::Cancel,
      "Escape maps to Cancel in menu");
  check(InputMapper::mapEvent(keyEvent(SDLK_PAGEUP), InputContext::Dialogue)
                .action == InputAction::Backlog,
        "PageUp maps to Backlog in dialogue");
  check(InputMapper::mapEvent(keyEvent(SDLK_PAGEUP), InputContext::Backlog)
                .action == InputAction::PageUp,
        "PageUp maps to PageUp in backlog");
  check(InputMapper::mapEvent(keyEvent(SDLK_LCTRL)).action ==
            InputAction::SkipHold,
        "LCtrl maps to SkipHold");
  check(InputMapper::mapEvent(keyEvent(SDLK_UP)).action == InputAction::NavUp,
        "Up arrow maps to NavUp");
  check(InputMapper::mapEvent(keyEvent(SDLK_DOWN)).action ==
            InputAction::NavDown,
        "Down arrow maps to NavDown");

  SDL_Event rmb{};
  rmb.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  rmb.button.button = SDL_BUTTON_RIGHT;
  rmb.button.x = 120.0f;
  rmb.button.y = 240.0f;
  const auto mappedRmb = InputMapper::mapEvent(rmb);
  check(mappedRmb.action == InputAction::Cancel && mappedRmb.pressed &&
            mappedRmb.hasPointer && mappedRmb.pointer.x == 120.0f,
        "Right-click maps to Cancel with pointer coordinates");

  SDL_Event wheel{};
  wheel.type = SDL_EVENT_MOUSE_WHEEL;
  wheel.wheel.y = 1.0f;
  wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
  check(InputMapper::mapEvent(wheel, InputContext::Dialogue).action ==
            InputAction::Backlog,
        "Wheel up opens Backlog in dialogue");
  wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
  check(InputMapper::mapEvent(wheel, InputContext::Dialogue).action ==
            InputAction::PageDown,
        "Flipped wheel up scrolls down");
}

void testGamepadMappingAndSwap() {
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_EAST),
                              InputContext::Menu, false)
                .action == InputAction::Confirm,
        "East confirms by default");
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_SOUTH),
                              InputContext::Menu, false)
                .action == InputAction::Cancel,
        "South cancels in menu by default");
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_SOUTH),
                              InputContext::Menu, true)
                .action == InputAction::Confirm,
        "Swapped A/B makes South confirm");
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_EAST),
                              InputContext::Menu, true)
                .action == InputAction::Cancel,
        "Swapped A/B makes East cancel");
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_START),
                              InputContext::Dialogue)
                .action == InputAction::Menu,
        "Start opens Menu");
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
                              InputContext::Dialogue)
                .action == InputAction::AutoToggle,
        "RB toggles Auto");
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
                              InputContext::Dialogue)
                .action == InputAction::Backlog,
        "LB opens Backlog");
  check(InputMapper::mapEvent(padButton(SDL_GAMEPAD_BUTTON_WEST),
                              InputContext::Dialogue)
                .action == InputAction::SkipToggle,
        "West toggles Skip");
  check(InputMapper::mapEvent(padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 20000),
                              InputContext::Dialogue)
                .action == InputAction::SkipHold,
        "Right trigger maps to SkipHold");
}

void testNavRepeaterAndStick() {
  InputMapper mapper(300, 100);
  // Left stick down -> emits initial DPAD_DOWN and arms repeater.
  auto initial = mapper.processDirectional(
      padAxis(SDL_GAMEPAD_AXIS_LEFTY, 24000, 4), 1000);
  check(initial.size() == 1 &&
            initial[0].type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            initial[0].gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        "Left stick emits virtual D-pad down");
  check(mapper.updateRepeat(1200).empty(),
        "No repeat before initial delay elapses");
  auto rep1 = mapper.updateRepeat(1300);
  check(rep1.size() == 1 &&
            rep1[0].gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        "First repeat fires at initialDelay");
  check(mapper.updateRepeat(1350).empty(),
        "No repeat before repeatInterval elapses");
  auto rep2 = mapper.updateRepeat(1400);
  check(rep2.size() == 1 &&
            rep2[0].gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        "Second repeat fires after repeatInterval");

  // Releasing stick cancels repeat.
  auto released =
      mapper.processDirectional(padAxis(SDL_GAMEPAD_AXIS_LEFTY, 0, 4), 1450);
  check(released.size() == 1 && released[0].type == SDL_EVENT_GAMEPAD_BUTTON_UP,
        "Centering stick releases virtual D-pad");
  check(mapper.updateRepeat(2000).empty(),
        "No repeat after stick returns to center");

  // Focus loss clears held D-pad repeat.
  mapper.processDirectional(padButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true, 9),
                            2000);
  SDL_Event lost{};
  lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  mapper.processDirectional(lost, 2100);
  check(mapper.updateRepeat(3000).empty(),
        "Focus loss clears held D-pad repeat");
}

void testTouchLongPressAndSwipeSkip() {
  InputActions actions;
  actions.process(fingerEvent(SDL_EVENT_FINGER_DOWN, 0.5f, 0.5f), 1000);
  check(!actions.pollLongPress(1400), "Long-press does not fire early");
  check(actions.pollLongPress(1600), "Long-press fires after threshold");
  check(!actions.pollLongPress(1700), "Long-press fires only once per hold");
  actions.process(fingerEvent(SDL_EVENT_FINGER_UP, 0.5f, 0.5f), 1800);
  check(!actions.tap(), "Long-press suppresses tap on finger release");

  // Interior swipe right -> skip gesture.
  actions.process(fingerEvent(SDL_EVENT_FINGER_DOWN, 0.3f, 0.5f), 2000);
  auto move = fingerEvent(SDL_EVENT_FINGER_MOTION, 0.55f, 0.51f);
  actions.process(move, 2050);
  check(actions.skip(move) && !actions.back(move) && !actions.backlog(move),
        "Interior swipe right triggers skip gesture");
  actions.process(fingerEvent(SDL_EVENT_FINGER_UP, 0.55f, 0.51f), 2100);
  check(!actions.tap(), "Swipe skip does not trigger tap");

  // PageUp opens backlog.
  check(actions.backlog(keyEvent(SDLK_PAGEUP)),
        "PageUp recognized by InputActions::backlog");
}

void testGamepadManagerHotplug() {
  GamepadManager manager;
  check(manager.count() == 0, "GamepadManager starts empty");
  SDL_Event removed{};
  removed.type = SDL_EVENT_GAMEPAD_REMOVED;
  removed.gdevice.which = 999;
  check(manager.handleEvent(removed), "GamepadManager handles removed event");
  check(manager.count() == 0, "Removing unknown gamepad is safe");
}

} // namespace

int main() {
  try {
    testTouchCoordinateMapping();
    testKeyboardAndMouseMapping();
    testGamepadMappingAndSwap();
    testNavRepeaterAndStick();
    testTouchLongPressAndSwipeSkip();
    testGamepadManagerHotplug();
    std::cout << "InputMapper tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "InputMapperTests failed: " << e.what() << '\n';
    return 1;
  }
}
