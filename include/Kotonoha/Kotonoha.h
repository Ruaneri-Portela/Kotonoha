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
  void *processPoolTasks;
  void *sound;
} Kotonoha_Game;

#define KOTONOHA_SCENE_CALL                                                    \
  SDL_Window *window, SDL_Renderer *render, void *userData, SDL_Texture *target

#define KOTONOHA_AUDIO_COMPONENTS                                              \
  int (*function)(void *parms, Uint8 **target, int *size),                     \
      void (*closeFunction)(void *parms), void *parms

#ifdef __cplusplus
}
#endif
