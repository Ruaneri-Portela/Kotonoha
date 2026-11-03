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
    std::cout << "Input action tests passed\n";
  } catch (std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
