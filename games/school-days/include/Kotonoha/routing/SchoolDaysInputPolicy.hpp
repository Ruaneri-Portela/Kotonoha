#pragma once

#include <SDL3/SDL.h>

namespace Kotonoha {

// Physical input boundary for authenticated School Days HQ v1.02 gameplay.
// Pointer hover/click are proved. Physical keyboard, gamepad and touch bindings
// are not, so they must not fall through to Kotonoha's generic Gameplay keys.
// Window events are presentation/lifecycle notifications, not narrative input,
// and remain allowed for the modern resizable/HiDPI viewport.
class SchoolDaysInputPolicy final {
public:
    static bool AcceptsGameplayEventType(Uint32 type) {
        switch (type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            return true;
        default:
            return false;
        }
    }
};

} // namespace Kotonoha
