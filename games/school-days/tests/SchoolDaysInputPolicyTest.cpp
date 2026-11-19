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
            Policy::AcceptsGameplayEventType(SDL_EVENT_MOUSE_BUTTON_DOWN) &&
            Policy::AcceptsGameplayEventType(SDL_EVENT_MOUSE_WHEEL) &&
            Policy::AcceptsGameplayEventType(SDL_EVENT_KEY_DOWN) &&
            Policy::AcceptsGameplayEventType(SDL_EVENT_KEY_UP),
        "authenticated pointer/keyboard/wheel events were rejected");

  Check(Policy::AcceptsMouseButton(SDL_BUTTON_LEFT) &&
            Policy::AcceptsMouseButton(SDL_BUTTON_RIGHT) &&
            !Policy::AcceptsMouseButton(SDL_BUTTON_MIDDLE) &&
            !Policy::AcceptsMouseButton(SDL_BUTTON_X1) &&
            !Policy::AcceptsMouseButton(SDL_BUTTON_X2),
        "mouse button filter drifted");

  using Cursor = Policy;
  const uint64_t motion = 1000u;
  const uint64_t deadline = motion + Cursor::kGameplayCursorAutoHideMs;
  Check(!Cursor::ShouldAutoHideGameplayCursor(true, false, false,
                                               deadline - 1u, motion) &&
            Cursor::ShouldAutoHideGameplayCursor(true, false, false,
                                               deadline, motion) &&
            Cursor::ShouldAutoHideGameplayCursor(true, false, false,
                                               deadline + 2000u, motion),
        "narrative-area cursor idle timeout changed");
  Check(!Cursor::ShouldAutoHideGameplayCursor(true, false, true,
                                               deadline + 2000u, motion) &&
            !Cursor::ShouldAutoHideGameplayCursor(true, true, false,
                                                deadline + 2000u, motion) &&
            !Cursor::ShouldAutoHideGameplayCursor(false, false, false,
                                                deadline + 2000u, motion),
        "MenuBar, modal, and title must keep the cursor visible");
  Check(!Cursor::ShouldAutoHideGameplayCursor(true, false, false, deadline, 0) &&
            !Cursor::ShouldAutoHideGameplayCursor(true, false, false,
                                                motion - 1u, motion) &&
            !Cursor::ShouldAutoHideGameplayCursor(true, false, false,
                                                deadline + 2000u,
                                                deadline + 1999u),
        "uninitialized, non-monotonic, or recent movement hid cursor");

    Check(Policy::AcceptsGameplayEventType(SDL_EVENT_WINDOW_RESIZED) &&
              Policy::AcceptsGameplayEventType(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
              Policy::AcceptsGameplayEventType(SDL_EVENT_WINDOW_DISPLAY_CHANGED),
          "presentation/lifecycle window events were rejected");

  Check(!Policy::AcceptsGameplayEventType(SDL_EVENT_MOUSE_BUTTON_UP) &&
            !Policy::AcceptsGameplayEventType(SDL_EVENT_TEXT_INPUT) &&
            !Policy::AcceptsGameplayEventType(SDL_EVENT_FINGER_DOWN) &&
            !Policy::AcceptsGameplayEventType(SDL_EVENT_GAMEPAD_BUTTON_DOWN) &&
            !Policy::AcceptsGameplayEventType(SDL_EVENT_GAMEPAD_AXIS_MOTION) &&
            !Policy::AcceptsGameplayEventType(SDL_EVENT_QUIT),
        "unauthenticated physical input leaked through gameplay boundary");

  using Acc = Policy::Accelerator;
  Check(Policy::TranslateAccelerator(SDLK_RETURN, SDL_KMOD_LALT) ==
                Acc::ToggleFullscreen &&
            Policy::TranslateAccelerator(SDLK_ESCAPE, SDL_KMOD_NONE) ==
                Acc::ExitFullscreen &&
            Policy::TranslateAccelerator(SDLK_Q, SDL_KMOD_LCTRL) ==
                Acc::QuitRequest &&
            Policy::TranslateAccelerator(SDLK_SPACE, SDL_KMOD_NONE) ==
                Acc::TogglePause &&
            Policy::TranslateAccelerator(SDLK_P, SDL_KMOD_RCTRL) ==
                Acc::TogglePause &&
            Policy::SpeedIndexForAccelerator(
                Policy::TranslateAccelerator(SDLK_1, SDL_KMOD_LCTRL)) == 0 &&
            Policy::SpeedIndexForAccelerator(
                Policy::TranslateAccelerator(SDLK_2, SDL_KMOD_LCTRL)) == 1 &&
            Policy::SpeedIndexForAccelerator(
                Policy::TranslateAccelerator(SDLK_4, SDL_KMOD_LCTRL)) == 2 &&
            Policy::SpeedIndexForAccelerator(
                Policy::TranslateAccelerator(SDLK_6, SDL_KMOD_LCTRL)) == 3 &&
            Policy::SpeedIndexForAccelerator(
                Policy::TranslateAccelerator(SDLK_8, SDL_KMOD_LCTRL)) == 4 &&
            Policy::TranslateAccelerator(SDLK_A, SDL_KMOD_LCTRL) ==
                Acc::ToggleMenuBarLock &&
            Policy::TranslateAccelerator(SDLK_B, SDL_KMOD_LCTRL) ==
                Acc::OpenConfig &&
            Policy::TranslateAccelerator(SDLK_C, SDL_KMOD_LCTRL) ==
                Acc::ToggleSubtitles &&
            Policy::TranslateAccelerator(SDLK_E, SDL_KMOD_LCTRL) ==
                Acc::OpenPlayDataFeeling &&
            Policy::TranslateAccelerator(SDLK_F, SDL_KMOD_LCTRL) ==
                Acc::SuperSkipNextChapter &&
            Policy::TranslateAccelerator(SDLK_K, SDL_KMOD_LCTRL) ==
                Acc::ReturnToTitle &&
            Policy::TranslateAccelerator(SDLK_L, SDL_KMOD_LCTRL) ==
                Acc::OpenLoad &&
            Policy::TranslateAccelerator(SDLK_M, SDL_KMOD_LCTRL) ==
                Acc::OpenExitConfirm &&
            Policy::TranslateAccelerator(SDLK_O, SDL_KMOD_LCTRL) ==
                Acc::OpenPlayDataHistory &&
            Policy::TranslateAccelerator(SDLK_R, SDL_KMOD_LCTRL) ==
                Acc::ToggleTopMeter &&
            Policy::TranslateAccelerator(SDLK_S, SDL_KMOD_LCTRL) ==
                Acc::OpenSave &&
            Policy::TranslateAccelerator(SDLK_T, SDL_KMOD_LCTRL) ==
                Acc::OpenBackLog &&
            Policy::TranslateAccelerator(SDLK_V, SDL_KMOD_LCTRL) ==
                Acc::OpenPlayDataRouteMap &&
            Policy::TranslateAccelerator(SDLK_F5, SDL_KMOD_NONE) == Acc::None &&
            Policy::TranslateAccelerator(SDLK_F6, SDL_KMOD_NONE) == Acc::None &&
            Policy::TranslateAccelerator(SDLK_LEFT, SDL_KMOD_NONE) == Acc::None,
        "retail 22-chord KeyAccelerator table drifted");

  using Key = Policy::MenuKey;
  Check(Policy::TranslateMenuKey(SDLK_UP, SDL_KMOD_NONE) == Key::Up &&
            Policy::TranslateMenuKey(SDLK_KP_8, SDL_KMOD_NONE) == Key::Up &&
            Policy::TranslateMenuKey(SDLK_DOWN, SDL_KMOD_NONE) == Key::Down &&
            Policy::TranslateMenuKey(SDLK_KP_2, SDL_KMOD_NONE) == Key::Down &&
            Policy::TranslateMenuKey(SDLK_LEFT, SDL_KMOD_NONE) == Key::Left &&
            Policy::TranslateMenuKey(SDLK_KP_4, SDL_KMOD_NONE) == Key::Left &&
            Policy::TranslateMenuKey(SDLK_RIGHT, SDL_KMOD_NONE) == Key::Right &&
            Policy::TranslateMenuKey(SDLK_KP_6, SDL_KMOD_NONE) == Key::Right &&
            Policy::TranslateMenuKey(SDLK_RETURN, SDL_KMOD_NONE) ==
                Key::Confirm &&
            Policy::TranslateMenuKey(SDLK_BACKSPACE, SDL_KMOD_NONE) ==
                Key::Cancel,
        "menu navigation key table drifted");

  std::cout << "SCHOOL DAYS INPUT POLICY GATE PASS\n"
            << "mouse_motion_click=PASS\n"
            << "cursor_idle_menubar=PASS\n"
            << "window_lifecycle=PASS\n"
            << "accelerators_22=PASS\n"
            << "menu_keys=PASS\n"
            << "gamepad_touch_rejected=PASS\n";
  return 0;
}
