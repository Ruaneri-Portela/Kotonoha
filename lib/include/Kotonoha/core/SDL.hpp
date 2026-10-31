#pragma once

#include <SDL3/SDL.h>
#include <memory>

namespace Kotonoha {

template <auto Destroy> struct SDLDeleter {
  template <typename T> void operator()(T *ptr) const noexcept { Destroy(ptr); }
};

template <typename T, auto Destroy>
using SDLHandle = std::unique_ptr<T, SDLDeleter<Destroy>>;

using CursorPtr = SDLHandle<SDL_Cursor, SDL_DestroyCursor>;
using TexturePtr = SDLHandle<SDL_Texture, SDL_DestroyTexture>;
using SurfacePtr = SDLHandle<SDL_Surface, SDL_DestroySurface>;
using WindowPtr = SDLHandle<SDL_Window, SDL_DestroyWindow>;
using RendererPtr = SDLHandle<SDL_Renderer, SDL_DestroyRenderer>;

using EventPtr = const SDL_Event *;
using EventRef = const SDL_Event &;

using WindowRef = SDL_Window &;
using RendererRef = SDL_Renderer &;
using CursorRef = SDL_Cursor &;
using TextureRef = SDL_Texture &;
using SurfaceRef = SDL_Surface &;

class Runtime;

class SDLSession {
public:
  explicit SDLSession(const Runtime &runtime);

  ~SDLSession();

  SDLSession(const SDLSession &) = delete;
  SDLSession &operator=(const SDLSession &) = delete;
};

} // namespace Kotonoha