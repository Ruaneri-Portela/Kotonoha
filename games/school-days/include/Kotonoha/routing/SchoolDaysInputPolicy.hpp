#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace Kotonoha {

// Physical input boundary & retail accelerator table for authenticated
// School Days HQ v1.02 (SCHOOLDAYS HQ.w.exe 0x0041c080, 0x00425f90,
// 0x00463cf0, 0x00465150, 0x00423ea0).
class SchoolDaysInputPolicy final {
public:
  enum class Accelerator {
    None = 0,
    // Global ExeModule accelerators (0x0041c080..0x0041c197)
    ToggleFullscreen = 0x8010, // Alt+Enter (0x8000000D)
    ExitFullscreen = 0x8011,   // Esc (0x0000001B) - fullscreen exit ONLY
    QuitRequest = 0x8012,      // Ctrl+Q (0x40000051) when mode >= 1
    // Gameplay FILMEngine accelerators (0x00422014..0x004220bd -> 0x00425870)
    TogglePause = 0x8001, // Space (0x00000020) or Ctrl+P (0x40000050)
    Speed1x = 0x8002,     // Ctrl+1 (0x40000031)
    Speed2x = 0x8021,     // Ctrl+2 (0x40000032)
    Speed4x = 0x8022,     // Ctrl+4 (0x40000034)
    Speed12x = 0x8023,    // Ctrl+6 (0x40000036)
    Speed24x = 0x8024,    // Ctrl+8 (0x40000038)
    OpenSave = 0x8003,    // Ctrl+S (0x40000053)
    OpenLoad = 0x8004,    // Ctrl+L (0x4000004C)
    ToggleSubtitles = 0x8005, // Ctrl+C (0x40000043)
    OpenConfig = 0x8006,      // Ctrl+B (0x40000042)
    ToggleMenuBarLock = 0x8007, // Ctrl+A (0x40000041)
    OpenPlayDataHistory = 0x8008, // Ctrl+O (0x4000004F)
    ToggleTopMeter = 0x8009,      // Ctrl+R (0x40000052)
    ReturnToTitle = 0x800A,       // Ctrl+K (0x4000004B)
    OpenBackLog = 0x800B,         // Ctrl+T (0x40000054)
    OpenExitConfirm = 0x800C,     // Ctrl+M (0x4000004D)
    OpenPlayDataFeeling = 0x800D, // Ctrl+E (0x40000045)
    SuperSkipNextChapter = 0x800E, // Ctrl+F (0x40000046)
    OpenPlayDataRouteMap = 0x800F, // Ctrl+V (0x40000056)
  };

  enum class MenuKey {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Cancel,
  };

  static constexpr uint32_t kLatchExpirationMs = 500u;
  static constexpr uint32_t kGameplayCursorAutoHideMs = 3000u;

  // A stationary pointer over the interactive MenuBar must stay visible.
  // Preserve the existing three-second timeout in the narrative area;
  // physical mouse motion restores the cursor, and modals always show it.
  static bool ShouldAutoHideGameplayCursor(bool gameplay, bool modal,
                                           bool pointerInsideMenuBar,
                                           uint64_t nowMs,
                                           uint64_t lastMouseMotionMs) {
    return gameplay && !modal && !pointerInsideMenuBar &&
           lastMouseMotionMs != 0 && nowMs >= lastMouseMotionMs &&
           nowMs - lastMouseMotionMs >= kGameplayCursorAutoHideMs;
  }

  // Event routing is not SetSELECT authorization: the runtime must still
  // reject keyboard, wheel and synthetic actions while a choice owns input.
  static bool AcceptsGameplayEventType(Uint32 type) {
    switch (type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
      return true;
    default:
      return false;
    }
  }

  static bool AcceptsMouseButton(Uint8 button) {
    return button == SDL_BUTTON_LEFT || button == SDL_BUTTON_RIGHT;
  }

  static Accelerator TranslateAccelerator(SDL_Keycode key, SDL_Keymod mod) {
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool alt = (mod & SDL_KMOD_ALT) != 0;

    // Alt+Enter (0x8000000D)
    if (alt && !ctrl && !shift &&
        (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
      return Accelerator::ToggleFullscreen;
    }
    if (alt || shift)
      return Accelerator::None;

    if (!ctrl) {
      if (key == SDLK_ESCAPE)
        return Accelerator::ExitFullscreen;
      if (key == SDLK_SPACE)
        return Accelerator::TogglePause;
      return Accelerator::None;
    }

    switch (key) {
    case SDLK_Q:
      return Accelerator::QuitRequest;
    case SDLK_P:
      return Accelerator::TogglePause;
    case SDLK_1:
      return Accelerator::Speed1x;
    case SDLK_2:
      return Accelerator::Speed2x;
    case SDLK_4:
      return Accelerator::Speed4x;
    case SDLK_6:
      return Accelerator::Speed12x;
    case SDLK_8:
      return Accelerator::Speed24x;
    case SDLK_A:
      return Accelerator::ToggleMenuBarLock;
    case SDLK_B:
      return Accelerator::OpenConfig;
    case SDLK_C:
      return Accelerator::ToggleSubtitles;
    case SDLK_E:
      return Accelerator::OpenPlayDataFeeling;
    case SDLK_F:
      return Accelerator::SuperSkipNextChapter;
    case SDLK_K:
      return Accelerator::ReturnToTitle;
    case SDLK_L:
      return Accelerator::OpenLoad;
    case SDLK_M:
      return Accelerator::OpenExitConfirm;
    case SDLK_O:
      return Accelerator::OpenPlayDataHistory;
    case SDLK_R:
      return Accelerator::ToggleTopMeter;
    case SDLK_S:
      return Accelerator::OpenSave;
    case SDLK_T:
      return Accelerator::OpenBackLog;
    case SDLK_V:
      return Accelerator::OpenPlayDataRouteMap;
    default:
      return Accelerator::None;
    }
  }

  static int SpeedIndexForAccelerator(Accelerator acc) {
    switch (acc) {
    case Accelerator::Speed1x:
      return 0;
    case Accelerator::Speed2x:
      return 1;
    case Accelerator::Speed4x:
      return 2;
    case Accelerator::Speed12x:
      return 3;
    case Accelerator::Speed24x:
      return 4;
    default:
      return -1;
    }
  }

  static MenuKey TranslateMenuKey(SDL_Keycode key, SDL_Keymod mod) {
    if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0)
      return MenuKey::None;
    switch (key) {
    case SDLK_UP:
    case SDLK_KP_8:
      return MenuKey::Up;
    case SDLK_DOWN:
    case SDLK_KP_2:
      return MenuKey::Down;
    case SDLK_LEFT:
    case SDLK_KP_4:
      return MenuKey::Left;
    case SDLK_RIGHT:
    case SDLK_KP_6:
      return MenuKey::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      return MenuKey::Confirm;
    case SDLK_BACKSPACE:
      return MenuKey::Cancel;
    default:
      return MenuKey::None;
    }
  }
};

} // namespace Kotonoha
