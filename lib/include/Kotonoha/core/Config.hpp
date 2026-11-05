#pragma once
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace Kotonoha {

enum class WindowMode { Windowed, Borderless, Exclusive };

// Platform detection helper for mobile devices (Android and iOS)
inline bool isMobilePlatform() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IOS)
  return true;
#else
  return false;
#endif
}

// Default window mode: Fullscreen (Borderless) on mobile, Windowed on desktop
inline WindowMode defaultWindowMode() {
  return isMobilePlatform() ? WindowMode::Borderless : WindowMode::Windowed;
}

// Default launcher state: Enabled on Linux, Windows, and Mobile
inline bool defaultLauncherEnabled() {
  return true;
#if defined(__linux__) || defined(_WIN32) || defined(__ANDROID__) ||           \
    (defined(__APPLE__) && TARGET_OS_IOS)
  return true;
#else
  return isMobilePlatform();
#endif
}

// Default font name used across the engine (SDL_ttf, ImGui, libass)
inline const char *defaultFontName() { return "NotoSansJP-Regular.ttf"; }

// Helper to check if display size and monitor settings can be changed
inline bool canChangeDisplaySize() { return !isMobilePlatform(); }

} // namespace Kotonoha