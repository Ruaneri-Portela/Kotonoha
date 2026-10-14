#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct Kotonoha_Time {
  Uint64 startTicks;
  Uint64 pausedTicks;
  Uint64 accumulatedPauseTicks;
  Uint64 seekTicks;
  bool started;
  bool paused;
  SDL_Mutex *mutex;
} Kotonoha_Time;

Kotonoha_Time *KotonohaTimeNew(bool startStopped);
void KotonohaTimeDestroy(Kotonoha_Time *instance);

void KotonohaTimeStart(Kotonoha_Time *instance);
void KotonohaTimeReset(Kotonoha_Time *instance, bool stopAfterReset);
void KotonohaTimePause(Kotonoha_Time *instance);
void KotonohaTimeResume(Kotonoha_Time *instance);

Uint64 KotonohaTimeGet(const Kotonoha_Time *instance);
void KotonohaTimeSet(Kotonoha_Time *instance, Uint64 time);

bool KotonohaTimeIsStarted(const Kotonoha_Time *instance);
bool KotonohaTimeIsPaused(const Kotonoha_Time *instance);
void KotonohaTimeSeekForward(Kotonoha_Time *instance, Uint64 delta);
void KotonohaTimeSeekBackward(Kotonoha_Time *instance, Uint64 delta);

Uint64 KotonohaTimeGetFromEvent(const Kotonoha_Time *instance, Uint64 start,
                                Uint64 end, bool *inRange, Sint64 *diff);
