#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace Kotonoha {

template <auto Destroy> struct SDLDeleter {
  template <typename T> void operator()(T *ptr) const noexcept { Destroy(ptr); }
};

template <typename T, auto Destroy>
using SDLHandle = std::unique_ptr<T, SDLDeleter<Destroy>>;

template <class T> using SDLMemoryPtr = SDLHandle<T, SDL_free>;
using IOStreamPtr = SDLHandle<SDL_IOStream, SDL_CloseIO>;

using CursorPtr = SDLHandle<SDL_Cursor, SDL_DestroyCursor>;
using TexturePtr = SDLHandle<SDL_Texture, SDL_DestroyTexture>;
using SurfacePtr = SDLHandle<SDL_Surface, SDL_DestroySurface>;
using WindowPtr = SDLHandle<SDL_Window, SDL_DestroyWindow>;
using RendererPtr = SDLHandle<SDL_Renderer, SDL_DestroyRenderer>;
using FontPtr = SDLHandle<TTF_Font, TTF_CloseFont>;
// Glyph atlas rendering through the current SDL renderer (GL/GLES when used).
using RendererTextEnginePtr =
    SDLHandle<TTF_TextEngine, TTF_DestroyRendererTextEngine>;
using TextPtr = SDLHandle<TTF_Text, TTF_DestroyText>;
using AudioStreamPtr = SDLHandle<SDL_AudioStream, SDL_DestroyAudioStream>;
using GamepadPtr = SDLHandle<SDL_Gamepad, SDL_CloseGamepad>;

using JoystickPtr = SDLHandle<SDL_Joystick, SDL_CloseJoystick>;
using SensorPtr = SDLHandle<SDL_Sensor, SDL_CloseSensor>;

// For arrays and strings allocated by SDL_GetDisplays, SDL_GetGamepads,
// SDL_GetAudioPlaybackDevices, SDL_GetPrefPath, etc. The memory is released
// with SDL_free automatically, including when exceptions are raised.
template <class T> using SDLArrayPtr = SDLMemoryPtr<T>;

// Some SDL resources are non-pointer IDs and cannot use std::unique_ptr.
template <typename ID, auto Close> class SDLValueHandle {
  ID id_{};

public:
  SDLValueHandle() = default;
  explicit SDLValueHandle(ID id) noexcept : id_(id) {}
  ~SDLValueHandle() { reset(); }
  SDLValueHandle(const SDLValueHandle &) = delete;
  SDLValueHandle &operator=(const SDLValueHandle &) = delete;
  SDLValueHandle(SDLValueHandle &&other) noexcept : id_(other.release()) {}
  SDLValueHandle &operator=(SDLValueHandle &&other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }
  ID get() const noexcept { return id_; }
  explicit operator bool() const noexcept { return id_ != ID{}; }
  ID release() noexcept {
    ID prior = id_;
    id_ = ID{};
    return prior;
  }
  void reset(ID next = ID{}) noexcept {
    if (id_ != ID{}) Close(id_);
    id_ = next;
  }
};

using AudioDeviceHandle = SDLValueHandle<SDL_AudioDeviceID, SDL_CloseAudioDevice>;


using EventPtr = const SDL_Event *;
using EventRef = const SDL_Event &;

using WindowRef = SDL_Window &;
using RendererRef = SDL_Renderer &;
using CursorRef = SDL_Cursor &;
using TextureRef = SDL_Texture &;
using SurfaceRef = SDL_Surface &;
using FontRef = TTF_Font &;
using AudioStreamRef = SDL_AudioStream &;

class Runtime;

class SDLSession {
  std::unordered_map<SDL_JoystickID, GamepadPtr> gamepads;
  void openGamepad(SDL_JoystickID id);

public:
  explicit SDLSession(const Runtime &runtime);

  ~SDLSession();
  void deviceEvent(EventRef event);
  size_t gamepadCount() const { return gamepads.size(); }

  SDLSession(const SDLSession &) = delete;
  SDLSession &operator=(const SDLSession &) = delete;
};

class TTFSession {
public:
  TTFSession() {
    if (!TTF_Init())
      throw std::runtime_error(SDL_GetError());
  }

  ~TTFSession() { TTF_Quit(); }

  TTFSession(const TTFSession &) = delete;
  TTFSession &operator=(const TTFSession &) = delete;
};

// SDL enables Metal's validation/debug layer (MTL_DEBUG_LAYER=1) for GPU
// renderers created in debug mode. Under Rosetta that layer drives the RGFX
// Metal shim through selectors its proxy objects do not implement
// (e.g. -[RgfxCmdBuffer isStatEnabled]), which throws
// NSInvalidArgumentException during a Core Animation commit. GPU validation
// is a developer tool, not a runtime requirement, so leave it off unless the
// caller explicitly opted in via SDL_RENDER_GPU_DEBUG / the environment.
inline void DisableGpuDebugByDefault() noexcept {
  if (!SDL_GetHint(SDL_HINT_RENDER_GPU_DEBUG)) {
    SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "0");
  }
}

// Metal's validation layer must be disabled before the first MTLDevice is
// created (Xcode/intentional env vars otherwise enable it globally). Once
// active, Core Animation's Metal commit path calls -isStatEnabled on the
// Rosetta RGFX proxy and crashes when a SwiftUI sheet opens.
inline void DisableMetalDebugEnvironment() noexcept {
#if defined(__APPLE__)
  ::unsetenv("MTL_DEBUG_LAYER");
  ::unsetenv("MTL_CAPTURE_ENABLED");
  ::unsetenv("MTL_HUD_ENABLED");
#endif
}

} // namespace Kotonoha
