#pragma once

// Each port supplies ONE class with static init/event/iterate/quit methods,
// then passes it to KOTONOHA_SDL_CALLBACK_APP(MyPort). SDL's four C callback
// symbols and their stable signatures live here, not in every game's main.
// Include this header before any SDL3/SDL_main.h include in a game entrypoint.
#ifndef SDL_MAIN_USE_CALLBACKS
#define SDL_MAIN_USE_CALLBACKS 1
#endif
#include <SDL3/SDL.h>
#include <Kotonoha/core/BuildVersion.hpp>
#include <SDL3/SDL_main.h>

// Port requirements:
//   static SDL_AppResult init(void **state, int argc, char **argv);
//   static SDL_AppResult event(void *state, SDL_Event *event);
//   static SDL_AppResult iterate(void *state);
//   static void quit(void *state, SDL_AppResult result);
// The port retains its per-game source discovery, launcher transition,
// exception reporting, and scene-quit policy; no generic callback may
// silently replace those semantics.
#define KOTONOHA_SDL_CALLBACK_APP(Port)                                      \
  SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {           \
    return Port::init(state, argc, argv);                                    \
  }                                                                         \
  SDL_AppResult SDL_AppEvent(void *state, SDL_Event *event) {                \
    return Port::event(state, event);                                        \
  }                                                                         \
  SDL_AppResult SDL_AppIterate(void *state) {                                \
    return Port::iterate(state);                                             \
  }                                                                         \
  void SDL_AppQuit(void *state, SDL_AppResult result) {                      \
    Port::quit(state, result);                                               \
  }

// SDL metadata's OS-facing version should stay numeric. The full Git-aware
// identity is available through --version, launcher and debug UI.
#define KOTONOHA_SET_APP_METADATA(title, identifier) \
  SDL_SetAppMetadata(title, Kotonoha::BuildVersion::Base, identifier)
