#pragma once

#include <Kotonoha/parsers/Config.h>
#include <SDL3/SDL.h>
#include <ass/ass.h>

#ifdef __cplusplus
extern "C" {
#endif

enum Kotonoha_Scene_Status {
  KOTONOHA_SCENE_NULL,
  KOTONOHA_SCENE_WAITING,
  KOTONOHA_SCENE_DRAW,
  KOTONOHA_SCENE_DRAW_LAST,
  KOTONOHA_SCENE_DRAW_OVERLAYED,
  KOTONOHA_SCENE_COMPLETE,
  KOTONOHA_SCENE_FATAL_ERROR,
  KOTONOHA_SCENE_CLOSE_APPLICATION
};

typedef struct Kotonoha_Game {
  ASS_Library *ass_library;
  ASS_Renderer *ass_renderer;
  ASS_Track *ass_track;

  SDL_Cursor *cursor;
  SDL_Window *window;
  SDL_Renderer *render;
  SDL_WindowFlags flags;
  SDL_Mutex *taskLock;

  Kotonoha_Config config;
  /* Optional profile-level runtime state.  Game frontends may project their
     own persistence/UI semantics onto these generic engine hooks. */
  SDL_AtomicInt menuVoiceEnabled;
  SDL_AtomicInt messageLayerEnabled;
  /* Non-owning root used by an active game/profile for authored resources that
     are distinct from the engine's own assetsPath. */
  const char *profileAssetsRoot;
  /* Optional narrative text-log sink.  The engine only publishes text that is
     actually reached by the running script; ownership stays with the game. */
  SDL_AtomicInt textLogCaptureEnabled;
  void *textLogUserdata;
  void (*textLogAppend)(void *userdata, const char *resolvedCharacter,
                        const char *resolvedText);
  void *processPoolTasks;
  void *sound;
} Kotonoha_Game;

static inline bool Kotonoha_IsMenuVoiceEnabled(const Kotonoha_Game *game) {
  return game != NULL &&
         SDL_GetAtomicInt((SDL_AtomicInt *)&game->menuVoiceEnabled) != 0;
}

static inline void Kotonoha_SetMenuVoiceEnabled(Kotonoha_Game *game,
                                                bool enabled) {
  if (game != NULL) {
    SDL_SetAtomicInt(&game->menuVoiceEnabled, enabled ? 1 : 0);
  }
}

static inline bool Kotonoha_IsMessageLayerEnabled(const Kotonoha_Game *game) {
  return game == NULL ||
         SDL_GetAtomicInt((SDL_AtomicInt *)&game->messageLayerEnabled) != 0;
}

static inline void Kotonoha_SetMessageLayerEnabled(Kotonoha_Game *game,
                                                   bool enabled) {
  if (game != NULL) {
    SDL_SetAtomicInt(&game->messageLayerEnabled, enabled ? 1 : 0);
  }
}

#define KOTONOHA_SCENE_CALL                                                    \
  SDL_Window *window, SDL_Renderer *render, void *userData, SDL_Texture *target

#define KOTONOHA_AUDIO_COMPONENTS                                              \
  int (*function)(void *parms, Uint8 **target, int *size),                     \
      void (*closeFunction)(void *parms), void *parms

#ifdef __cplusplus
}
#endif
