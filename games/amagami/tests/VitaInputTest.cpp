#include <Amagami/VitaInput.hpp>
#include <Amagami/VitaVmBridge.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
SDL_Event key(SDL_EventType type, SDL_Keycode code) {
  SDL_Event e{};
  e.type = type;
  e.key.key = code;
  return e;
}
SDL_Event button(SDL_EventType type, SDL_JoystickID device, uint8_t name) {
  SDL_Event e{};
  e.type = type;
  e.gbutton.which = device;
  e.gbutton.button = name;
  return e;
}
SDL_Event axis(SDL_JoystickID device, uint8_t name, Sint16 value) {
  SDL_Event e{};
  e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  e.gaxis.which = device;
  e.gaxis.axis = name;
  e.gaxis.value = value;
  return e;
}
} // namespace

int main() {
  try {
    Amagami::Vita::VitaInput input;
    using Amagami::Vita::VitaInput;
    expect(VitaInput::mapGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH,
                                       SDL_GAMEPAD_TYPE_XBOXONE) == 32u &&
               VitaInput::mapGamepadButton(SDL_GAMEPAD_BUTTON_EAST,
                                           SDL_GAMEPAD_TYPE_XBOXONE) == 64u,
           "Xbox A confirms and B cancels");
    expect(VitaInput::mapGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH,
                                       SDL_GAMEPAD_TYPE_XBOX360) == 32u &&
               VitaInput::mapGamepadButton(SDL_GAMEPAD_BUTTON_EAST,
                                           SDL_GAMEPAD_TYPE_PS5) == 32u &&
               VitaInput::mapGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH,
                                           SDL_GAMEPAD_TYPE_PS5) == 64u,
           "Xbox 360 and PlayStation button layouts are distinct");
    expect(VitaInput::mapGamepadButton(SDL_GAMEPAD_BUTTON_EAST,
                                       SDL_GAMEPAD_TYPE_UNKNOWN) == 32u &&
               VitaInput::mapGamepadButton(SDL_GAMEPAD_BUTTON_NORTH,
                                           SDL_GAMEPAD_TYPE_XBOXONE) == 16u,
           "Fallback controller layout leaves other buttons unchanged");
    Amagami::Vita::VitaVmBridge bridge;
    input.handle(key(SDL_EVENT_KEY_DOWN, SDLK_Z));
    input.handle(key(SDL_EVENT_KEY_DOWN, SDLK_UP));
    expect(input.buttons() == (32u | 4096u), "Vita keyboard confirm and up");
    bridge.inputFrame(input.buttons(), input.analog());
    expect(bridge.vitaButtonsPressed() == (0x2000u | 0x10u),
           "Real SDL input maps to SceCtrl script trigger bits");
    input.handle(key(SDL_EVENT_KEY_DOWN, SDLK_Z));
    bridge.inputFrame(input.buttons(), input.analog());
    expect(bridge.vitaButtonsPressed() == 0, "Holding key not a new trigger");

    // Both Z and Space activate Circle: key-up for one must not undo the
    // other. Duplicate key-down events must also be idempotent.
    input.handle(key(SDL_EVENT_KEY_DOWN, SDLK_SPACE));
    input.handle(key(SDL_EVENT_KEY_UP, SDLK_Z));
    expect(input.buttons() == (32u | 4096u),
           "Releasing Z preserves Circle while Space is still pressed");
    input.handle(key(SDL_EVENT_KEY_DOWN, SDLK_Z));
    input.handle(key(SDL_EVENT_KEY_UP, SDLK_SPACE));
    expect(input.buttons() == (32u | 4096u),
           "Releasing Space preserves Circle while Z is still pressed");

    input.handle(
        button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 77, SDL_GAMEPAD_BUTTON_EAST));
    expect(input.buttons() == (32u | 4096u),
           "Keyboard and gamepad sources share confirm without duplication");
    input.handle(key(SDL_EVENT_KEY_UP, SDLK_Z));
    expect(input.buttons() == (32u | 4096u),
           "Gamepad continues holding confirm after keyboard keyup");
    input.handle(button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 77,
                        SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    expect((input.buttons() & 32768u) != 0, "Vita gamepad dpad-left mapping");
    // Ignore extra gamepads while the primary device is active.
    input.handle(
        button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 101, SDL_GAMEPAD_BUTTON_NORTH));
    expect((input.buttons() & 16u) == 0,
           "Secondary gamepad does not alter the primary mapping");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFTX, 16384));
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFTY, -8192));
    expect(input.analog()[0] == 64 && input.analog()[1] == -32,
           "Analog stick values converted to signed Vita range");
    expect((input.buttons() & 8192u) != 0,
           "Left stick crosses the dead zone and activates D-pad right");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFTX, 15999));
    expect((input.buttons() & 8192u) == 0 && (input.buttons() & 32768u) != 0,
           "Recentering the stick preserves the physically held D-pad left");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFTY, -20000));
    expect((input.buttons() & 4096u) != 0,
           "Left stick up is also a navigation button");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFTY, 0));
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFTX, -32768));
    expect(input.analog()[0] == -128 && (input.buttons() & 32768u) != 0,
           "Left analog axis clamps to the signed Vita range");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 28000));
    expect((input.buttons() & 1u) == 1u, "Left trigger maps to L2");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 4001));
    expect((input.buttons() & 1u) != 0,
           "Reference SDL threshold accepts partially pressed triggers");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 4000));
    expect((input.buttons() & 1u) == 0,
           "Trigger threshold is strict and clears only trigger input");
    input.handle(axis(77, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0));
    expect((input.buttons() & 1u) == 0, "Left trigger release clears L2");
    input.handle(key(SDL_EVENT_KEY_DOWN, SDLK_RETURN));
    expect((input.buttons() & 2048u) != 0, "Keyboard Start mapping");

    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.handle(focus);
    expect((input.buttons() & 2048u) == 0 && (input.buttons() & 4096u) == 0,
           "Window focus loss releases keyboard input only");
    SDL_Event removed{};
    removed.type = SDL_EVENT_GAMEPAD_REMOVED;
    removed.gdevice.which = 77;
    input.handle(removed);
    expect(input.buttons() == 0 && input.analog()[0] == 0,
           "Disconnect clears gamepad buttons and axes");
    input.handle(
        button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 101, SDL_GAMEPAD_BUTTON_NORTH));
    expect(input.buttons() == 16u,
           "A new primary gamepad works after disconnection");
    std::cout << "Vita SDL3 event/input adapter tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
