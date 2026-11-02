#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <memory>
#include <stdexcept>

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
using AudioStreamPtr = SDLHandle<SDL_AudioStream, SDL_DestroyAudioStream>;

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
public:
  explicit SDLSession(const Runtime &runtime);

  ~SDLSession();

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

} // namespace Kotonoha
