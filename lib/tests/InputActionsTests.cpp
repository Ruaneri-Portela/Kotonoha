#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/events/InputActions.hpp>
#include <iostream>
#include <stdexcept>
using namespace Kotonoha;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
SDL_Event finger(Uint32 type, int id, float x, float y) {
  SDL_Event e{};
  e.type = type;
  e.tfinger.touchID = 1;
  e.tfinger.fingerID = id;
  e.tfinger.x = x;
  e.tfinger.y = y;
  return e;
}
SDL_Event pad(SDL_GamepadButton button, bool down = true) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
  e.gbutton.button = button;
  e.gbutton.down = down;
  return e;
}
int main() {
  try {
    InputActions a, b;
    auto down = finger(SDL_EVENT_FINGER_DOWN, 1, .5, .8);
    a.process(down);
    auto up = finger(SDL_EVENT_FINGER_MOTION, 1, .5, .6);
    a.process(up);
    check(a.backlog(up) && !a.back(up), "Swipe opens history");
    b.process(up);
    check(!b.backlog(up), "Engine gesture states are independent");
    a.process(finger(SDL_EVENT_FINGER_UP, 1, .5, .6));
    check(!a.tap(), "Swipe never becomes a tap");
    a.process(down);
    a.process(finger(SDL_EVENT_FINGER_DOWN, 2, .6, .8));
    auto lift = finger(SDL_EVENT_FINGER_UP, 1, .5, .8);
    a.process(lift);
    check(a.back(lift), "Two-finger tap goes back");
    a.process(finger(SDL_EVENT_FINGER_UP, 2, .6, .8));
    check(!a.back(lift) && !a.tap(), "Gesture triggers once");
    a.process(down);
    a.process(finger(SDL_EVENT_FINGER_UP, 1, .5, .8));
    check(a.tap(), "Single tap recognized");
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1;
    wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    check(!a.backlog(wheel), "Flipped wheel honors direction");
    ScrollGesture scroll;
    float target = 100, velocity = 0;
    bool follow = true;
    scroll.handle(down, 720, target, velocity, follow, 500);
    scroll.handle(up, 720, target, velocity, follow, 500);
    check(target > 200 && !follow, "Touch scroll moves content");
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    a.process(lost);
    check(!a.tap() && !a.backlog(up), "Focus loss resets gestures");
    scroll.handle(finger(SDL_EVENT_FINGER_CANCELED, 1, .5f, .6f), 720, target,
                  velocity, follow, 500);
    check(!scroll.isDragging(), "Canceled touch releases scroll drag");
    ControllerNavigation navigation;
    SDL_Event axis{};
    axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    axis.gaxis.which = 7;
    axis.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTY;
    axis.gaxis.value = 20000;
    auto buttons = navigation.process(axis);
    check(buttons.size() == 1 && buttons[0].gbutton.down &&
              MapControllerEvent(buttons[0]).key.key == SDLK_DOWN,
          "Left stick navigates through the shared controller mapping");
    axis.gaxis.value = 15000;
    check(navigation.process(axis).empty(),
          "Analog hysteresis avoids duplicate focus movement");
    axis.gaxis.value = -20000;
    buttons = navigation.process(axis);
    check(buttons.size() == 2 && !buttons[0].gbutton.down &&
              buttons[1].gbutton.down &&
              MapControllerEvent(buttons[1]).key.key == SDLK_UP,
          "Crossing stick direction releases the previous button first");
    axis.gaxis.which = 8;
    check(navigation.process(axis).size() == 1,
          "Controller axes are independent per device");
    axis.gaxis.which = 7;
    axis.gaxis.value = 0;
    buttons = navigation.process(axis);
    check(buttons.size() == 1 && !buttons[0].gbutton.down,
          "Returning stick to center releases navigation");
    navigation.process(lost);
    check(navigation.process(axis).empty(), "Focus loss clears analog state");
    check(MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_EAST)).key.key ==
                  SDLK_RETURN &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH)).key.key ==
                  SDLK_AC_BACK &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH), true).key.key ==
                  SDLK_LCTRL,
          "Controller confirm/cancel mapping honors the dialogue context");
    check(MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_WEST)).key.key == SDLK_S &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_NORTH)).key.key ==
                  SDLK_AC_BACK &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_START)).key.key ==
                  SDLK_ESCAPE &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
                      .key.key == SDLK_A,
          "Controller shortcuts map to skip, pause and auto");
    check(MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_BACK)).key.key ==
                  SDLK_PAGEUP &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), true,
                                 true)
                      .key.key == SDLK_ESCAPE &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_BACK), true, true)
                      .key.key == SDLK_ESCAPE,
          "History mapping closes with Back instead of scrolling");
    check(MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT)).key.key ==
                  SDLK_LEFT &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_MISC1)).type ==
                  SDL_EVENT_GAMEPAD_BUTTON_DOWN,
          "Directional mapping keeps menu navigation and unmapped buttons "
          "intact");
    check(MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT), true).key.key ==
                  SDLK_MINUS &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), true)
                      .key.key == SDLK_PLUS &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_UP)).key.key ==
                  SDLK_UP &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN)).key.key ==
                  SDLK_DOWN &&
              MapControllerEvent(pad(SDL_GAMEPAD_BUTTON_EAST, false)).type ==
                  SDL_EVENT_KEY_UP,
          "D-pad mapping and button releases stay usable in dialogue");
    const auto swappedSouth =
        Kotonoha::NormalizeControllerEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH), true);
    const auto swappedEast =
        Kotonoha::NormalizeControllerEvent(pad(SDL_GAMEPAD_BUTTON_EAST), true);
    check(swappedSouth.gbutton.button == SDL_GAMEPAD_BUTTON_EAST &&
              MapControllerEvent(swappedSouth).key.key == SDLK_RETURN &&
              swappedEast.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH &&
              MapControllerEvent(swappedEast).key.key == SDLK_AC_BACK,
          "A/B swap exchanges physical confirm and cancel");
    check(NormalizeControllerEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_UP), true)
                      .gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP &&
              NormalizeControllerEvent(pad(SDL_GAMEPAD_BUTTON_WEST), true)
                      .gbutton.button == SDL_GAMEPAD_BUTTON_WEST &&
              NormalizeControllerEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH), false)
                      .gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH,
          "A/B swap leaves directions, other buttons and the disabled layout "
          "untouched");
    SDL_Event keyboard{};
    keyboard.type = SDL_EVENT_KEY_DOWN;
    keyboard.key.key = SDLK_ESCAPE;
    check(MapControllerEvent(keyboard).key.key == SDLK_ESCAPE,
          "Non-controller events pass through unchanged");
    std::cout << "Input action tests passed\n";
  } catch (std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
