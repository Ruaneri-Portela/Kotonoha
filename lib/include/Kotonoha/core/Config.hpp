#pragma once
// Feature switches shared by CMake, native Xcode and all engine clients.
// An OFF module has no runtime services or visible controls.
#ifndef KOTONOHA_ENABLE_UPSCALER
# if defined(__EMSCRIPTEN__)
#  define KOTONOHA_ENABLE_UPSCALER 0
# else
#  define KOTONOHA_ENABLE_UPSCALER 1
# endif
#endif
#ifndef KOTONOHA_ENABLE_MCP
# if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#  define KOTONOHA_ENABLE_MCP 0
# elif defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_IPHONE
#   define KOTONOHA_ENABLE_MCP 0
#  else
#   define KOTONOHA_ENABLE_MCP 1
#  endif
# else
#  define KOTONOHA_ENABLE_MCP 1
# endif
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
// Discord RPC exists only where a local IPC endpoint is available: Windows,
// Linux and macOS desktop, plus opt-in Android. Never on WASM or iOS.
#ifndef KOTONOHA_HAS_DISCORD_RPC
# if defined(__EMSCRIPTEN__)
#  define KOTONOHA_HAS_DISCORD_RPC 0
# elif defined(__ANDROID__)
#  ifdef KOTONOHA_ANDROID_DISCORD_RPC
#   define KOTONOHA_HAS_DISCORD_RPC 1
#  else
#   define KOTONOHA_HAS_DISCORD_RPC 0
#  endif
# elif defined(__APPLE__) && TARGET_OS_IPHONE
#  define KOTONOHA_HAS_DISCORD_RPC 0
# else
#  define KOTONOHA_HAS_DISCORD_RPC 1
# endif
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

// Web builds use browser media APIs and may prefer WebM/MP4 sidecars over
// retail AVI. Keep toolchain detection inside the engine, not game sources.
inline bool isBrowserPlatform() {
#if defined(__EMSCRIPTEN__)
  return true;
#else
  return false;
#endif
}

// Default window mode: Fullscreen (Borderless) on mobile, Windowed on desktop
inline WindowMode defaultWindowMode() {
  return isMobilePlatform() ? WindowMode::Borderless : WindowMode::Windowed;
}

// Every game port uses the same launcher default on desktop, mobile and
// browser builds. Automated invocations opt out in CommandLine::parse().
inline bool defaultLauncherEnabled() { return true; }

// Default font name used across the engine (SDL_ttf, ImGui, libass)
inline const char *defaultFontName() { return "NotoSansJP-Regular.ttf"; }

// Helper to check if display size and monitor settings can be changed
inline bool canChangeDisplaySize() { return !isMobilePlatform(); }

} // namespace Kotonoha