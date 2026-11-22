// C++17 regression tests for deliberate media-skip input.
#include <Kotonoha/events/VideoSkipGesture.hpp>

#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

SDL_Event mouse(float x, float y, Uint8 clicks = 1,
                Uint8 button = SDL_BUTTON_LEFT) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.button = button;
  event.button.clicks = clicks;
  event.button.x = x;
  event.button.y = y;
  return event;
}

SDL_Event touch(float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_FINGER_DOWN;
  event.tfinger.x = x;
  event.tfinger.y = y;
  return event;
}
} // namespace

int main() {
  try {
    require(!Kotonoha::VideoSkipGesture::capturesInput(true, true),
            "Looping menu video must not capture navigation");
    require(Kotonoha::VideoSkipGesture::capturesInput(true, false),
            "Narrative cutscene must capture input");
    require(!Kotonoha::VideoSkipGesture::capturesInput(false, false),
            "Inactive movie must not capture input");
    Kotonoha::VideoSkipGesture gesture;
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    require(!gesture.accept(e, 100), "Mouse motion must not skip");
    e.type = SDL_EVENT_MOUSE_WHEEL;
    require(!gesture.accept(e, 120), "Scroll must not skip");
    e.type = SDL_EVENT_WINDOW_RESIZED;
    require(!gesture.accept(e, 130), "Window resize must not skip");
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    require(!gesture.accept(e, 150), "Mouse release must not skip");

    require(!gesture.accept(mouse(100, 100), 1000),
            "Single mouse click must not skip");
    require(gesture.accept(mouse(110, 105), 1200),
            "Two nearby clicks within 350ms should skip");

    gesture.reset();
    require(!gesture.accept(mouse(50, 50), 2000), "First press arms");
    require(!gesture.accept(mouse(50, 50), 2400),
            "Clicks more than 350ms apart must not skip");
    require(gesture.accept(mouse(50, 50), 2500),
            "The recent pair after timeout is a double click");

    gesture.reset();
    require(!gesture.accept(mouse(50, 50), 3000), "First press arms again");
    require(!gesture.accept(mouse(200, 200), 3200),
            "Distant clicks must not be treated as a double click");
    require(!gesture.accept(mouse(50, 50, 1, SDL_BUTTON_RIGHT), 3220),
            "Right-click is not a skip gesture");

    gesture.reset();
    e = mouse(10, 10);
    e.button.which = SDL_TOUCH_MOUSEID;
    require(!gesture.accept(e, 3400),
            "Synthetic touch mouse events must not duplicate touches");
    require(!gesture.accept(touch(0.5f, 0.5f), 3500),
            "A single finger tap must not skip");
    require(gesture.accept(touch(0.51f, 0.50f), 3650),
            "A double finger tap should skip");

    gesture.reset();
    require(gesture.accept(mouse(1, 1, 2), 4000),
            "SDL-provided double-click counts should be respected");
    e = {};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.repeat = false;
    require(gesture.accept(e, 5000), "Keyboard key should skip");
    e.key.repeat = true;
    require(!gesture.accept(e, 5100), "Keyboard repeats must not skip");
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    require(gesture.accept(e, 5200), "Controller press should skip");
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    require(!gesture.accept(e, 5300), "Controller release must not skip");

    std::cout << "Video skip gestures passed\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Video skip gestures failed: " << ex.what() << '\n';
    return 1;
  }
}
