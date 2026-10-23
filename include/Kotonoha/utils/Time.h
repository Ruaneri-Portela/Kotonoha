#pragma once

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kotonoha_Time {
  Uint64 startTicks;
  Uint64 pausedTicks;
  Uint64 accumulatedPauseTicks;
  Uint64 seekTicks;
  Uint64 discontinuitySerial;
  double rate;
  bool started;
  bool paused;
  SDL_Mutex *mutex;
} Kotonoha_Time;

/*
 * Compatibility surface for the legacy profile runtime. The public clock
 * type/functions were renamed during the engine refactor; keep both spellings
 * backed by the same implementation so runtime semantics do not fork.
 */
typedef Kotonoha_Time Kotonoha_time;

Kotonoha_Time *KotonohaTimeNew(bool startStopped);
void KotonohaTimeDestroy(Kotonoha_Time *instance);

void KotonohaTimeStart(Kotonoha_Time *instance);
void KotonohaTimeReset(Kotonoha_Time *instance, bool stopAfterReset);
void KotonohaTimePause(Kotonoha_Time *instance);
void KotonohaTimeResume(Kotonoha_Time *instance);

Uint64 KotonohaTimeGet(const Kotonoha_Time *instance);
void KotonohaTimeSet(Kotonoha_Time *instance, Uint64 time);
/* Change logical timeline speed without changing the current logical position. */
bool KotonohaTimeSetRate(Kotonoha_Time *instance, double rate);
double KotonohaTimeGetRate(const Kotonoha_Time *instance);
/* Changes only for explicit timeline discontinuities, never normal rate flow. */
Uint64 KotonohaTimeGetDiscontinuitySerial(const Kotonoha_Time *instance);

bool KotonohaTimeIsStarted(const Kotonoha_Time *instance);
bool KotonohaTimeIsPaused(const Kotonoha_Time *instance);
void KotonohaTimeSeekForward(Kotonoha_Time *instance, Uint64 delta);
void KotonohaTimeSeekBackward(Kotonoha_Time *instance, Uint64 delta);

Uint64 KotonohaTimeGetFromEvent(const Kotonoha_Time *instance, Uint64 start,
                                Uint64 end, bool *inRange, Sint64 *diff);

static inline Kotonoha_time *Kotonoha_timeNew(bool startStopped) {
  return KotonohaTimeNew(startStopped);
}
static inline void Kotonoha_timeDestroy(Kotonoha_time *instance) {
  KotonohaTimeDestroy(instance);
}
static inline void Kotonoha_timeStart(Kotonoha_time *instance) {
  KotonohaTimeStart(instance);
}
static inline void Kotonoha_timeReset(Kotonoha_time *instance,
                                      bool stopAfterReset) {
  KotonohaTimeReset(instance, stopAfterReset);
}
static inline void Kotonoha_timePause(Kotonoha_time *instance) {
  KotonohaTimePause(instance);
}
static inline void Kotonoha_timeResume(Kotonoha_time *instance) {
  KotonohaTimeResume(instance);
}
static inline Uint64 Kotonoha_timeGet(const Kotonoha_time *instance) {
  return KotonohaTimeGet(instance);
}
static inline void Kotonoha_timeSet(Kotonoha_time *instance, Uint64 time) {
  KotonohaTimeSet(instance, time);
}
static inline bool Kotonoha_timeSetRate(Kotonoha_time *instance, double rate) {
  return KotonohaTimeSetRate(instance, rate);
}
static inline double Kotonoha_timeGetRate(const Kotonoha_time *instance) {
  return KotonohaTimeGetRate(instance);
}
static inline Uint64
Kotonoha_timeGetDiscontinuitySerial(const Kotonoha_time *instance) {
  return KotonohaTimeGetDiscontinuitySerial(instance);
}
static inline bool Kotonoha_timeIsStarted(const Kotonoha_time *instance) {
  return KotonohaTimeIsStarted(instance);
}
static inline bool Kotonoha_timeIsPaused(const Kotonoha_time *instance) {
  return KotonohaTimeIsPaused(instance);
}
static inline void Kotonoha_timeSeekForward(Kotonoha_time *instance,
                                            Uint64 delta) {
  KotonohaTimeSeekForward(instance, delta);
}
static inline void Kotonoha_timeSeekBackward(Kotonoha_time *instance,
                                             Uint64 delta) {
  KotonohaTimeSeekBackward(instance, delta);
}
static inline Uint64 Kotonoha_timeGetFromEvent(const Kotonoha_time *instance,
                                               Uint64 start, Uint64 end,
                                               bool *inRange, Sint64 *diff) {
  return KotonohaTimeGetFromEvent(instance, start, end, inRange, diff);
}

#ifdef __cplusplus
}
#endif
