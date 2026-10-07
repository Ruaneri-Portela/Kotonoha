#pragma once
/*
This a part of Kotonoha project, copyright 2024

# Kotonoha.h

Kotonoha.h is a C binding for Kotonoha.hpp
How as SDL3 define as some subrotines from replace classic main function,
Kotonoha::Kotonoha class is exposed from a (void)* pointer

The global context is as defined by appContext

# KOTONOHA_SCENE_CALL
To KOTONOHA_SCENE_CALL macro, is to two uses for renders and scenes

Your mission is passed render control to new funciontion, to keep event stack as
uses a custrom linked array named as eventQueu, when uses inside
Kotonoha_SCENE_CALL DONT forget NOT CLEAN THE EVENT STACK, this is a job for
Kotonoha::Kotonoha::Main() method
*/

#include <Kotonoha/utils/UserEvents.h>
#include <SDL3/SDL.h>
#include <ass/ass.h>

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

struct Kotonoha_Game {
  ASS_Library *ass_library;
  ASS_Renderer *ass_renderer;
  void *sound;

  SDL_Window *window;
  SDL_Renderer *render;
  SDL_WindowFlags flags;
  SDL_Mutex *taskLock;

  char *assetsPath, *configPath, *styleStr;

  bool showFps, showTimestamp, paused, back, next;
  int vsync, scene;

  /* Persistent School Days runtime setting; UI wiring is a later gate. */
  SDL_AtomicInt schoolDaysMenVoiceEnabled;

  struct Kotonoha_eventStack eventQueu;
  void *processPoolTasks;
};

static inline bool Kotonoha_IsMenVoiceEnabled(struct Kotonoha_Game *game) {
  return game != NULL &&
         SDL_GetAtomicInt(&game->schoolDaysMenVoiceEnabled) != 0;
}

static inline void Kotonoha_SetMenVoiceEnabled(struct Kotonoha_Game *game,
                                               bool enabled) {
  if (game != NULL)
    SDL_SetAtomicInt(&game->schoolDaysMenVoiceEnabled, enabled ? 1 : 0);
}

#define KOTONOHA_SCENE_CALL                                                    \
  SDL_Window *window, SDL_Renderer *render,                                    \
      struct Kotonoha_eventStack *eventQueu, void *userData,                   \
      SDL_Texture *target

#define KOTONOHA_AUDIO_COMPONENTS                                              \
  int (*function)(void *parms, Uint8 **target, int *size),                     \
      void (*closeFunction)(void *parms), void *parms
