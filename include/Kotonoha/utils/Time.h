#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct Kotonoha_time {
  Uint64 startTicks;
  Uint64 pausedTicks;
  Uint64 accumulatedPauseTicks;
  Uint64 seekTicks;
  bool started;
  bool paused;
} Kotonoha_time;

Kotonoha_time *Kotonoha_timeNew(bool startStopped);
void Kotonoha_timeDestroy(Kotonoha_time *instance);

void Kotonoha_timeStart(Kotonoha_time *instance);
void Kotonoha_timeReset(Kotonoha_time *instance, bool stopAfterReset);
void Kotonoha_timePause(Kotonoha_time *instance);
void Kotonoha_timeResume(Kotonoha_time *instance);

Uint64 Kotonoha_timeGet(const Kotonoha_time *instance);
void Kotonoha_timeSet(Kotonoha_time *instance, Uint64 time);

bool Kotonoha_timeIsStarted(const Kotonoha_time *instance);
bool Kotonoha_timeIsPaused(const Kotonoha_time *instance);
void Kotonoha_timeSeekForward(Kotonoha_time *instance, Uint64 delta);
void Kotonoha_timeSeekBackward(Kotonoha_time *instance, Uint64 delta);

Uint64 Kotonoha_timeGetFromEvent(const Kotonoha_time *instance, Uint64 start,
                                 Uint64 end, bool *inRange, Sint64 *diff);
