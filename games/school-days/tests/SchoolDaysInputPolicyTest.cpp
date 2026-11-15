#include <Kotonoha/routing/SchoolDaysInputPolicy.hpp>

#include <cstdlib>
#include <iostream>

namespace {

using Policy = Kotonoha::SchoolDaysInputPolicy;

[[noreturn]] void Fail(const char* message) {
    std::cerr << "SchoolDaysInputPolicyTest: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

} // namespace

int main() {
    Check(Policy::AcceptsGameplayEventType(SDL_EVENT_MOUSE_MOTION) &&
              Policy::AcceptsGameplayEventType(SDL_EVENT_MOUSE_BUTTON_DOWN),
          "authenticated pointer events were rejected");

    Check(Policy::AcceptsGameplayEventType(SDL_EVENT_WINDOW_RESIZED) &&
              Policy::AcceptsGameplayEventType(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
              Policy::AcceptsGameplayEventType(SDL_EVENT_WINDOW_DISPLAY_CHANGED),
          "presentation/lifecycle window events were rejected");

    Check(!Policy::AcceptsGameplayEventType(SDL_EVENT_MOUSE_BUTTON_UP) &&
              !Policy::AcceptsGameplayEventType(SDL_EVENT_MOUSE_WHEEL) &&
              !Policy::AcceptsGameplayEventType(SDL_EVENT_KEY_DOWN) &&
              !Policy::AcceptsGameplayEventType(SDL_EVENT_TEXT_INPUT) &&
              !Policy::AcceptsGameplayEventType(SDL_EVENT_FINGER_DOWN) &&
              !Policy::AcceptsGameplayEventType(SDL_EVENT_GAMEPAD_BUTTON_DOWN) &&
              !Policy::AcceptsGameplayEventType(SDL_EVENT_GAMEPAD_AXIS_MOTION) &&
              !Policy::AcceptsGameplayEventType(SDL_EVENT_QUIT),
          "unauthenticated physical input leaked through gameplay boundary");

    std::cout << "SCHOOL DAYS INPUT POLICY GATE PASS\n"
              << "mouse_motion_click=PASS\n"
              << "window_lifecycle=PASS\n"
              << "keyboard_gamepad_touch_rejected=PASS\n"
              << "unproved_mouse_events_rejected=PASS\n";
    return 0;
}
