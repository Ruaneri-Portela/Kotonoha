#include <Kotonoha/utils/Log.h>
#include <Kotonoha/utils/Time.h>

static Uint64 KotonohaTimeNow(void) { return SDL_GetTicks(); }

static void KotonohaTimeLock(const Kotonoha_Time *instance) {
  if (instance != NULL && instance->mutex != NULL) {
    SDL_LockMutex(instance->mutex);
  }
}

static void KotonohaTimeUnlock(const Kotonoha_Time *instance) {
  if (instance != NULL && instance->mutex != NULL) {
    SDL_UnlockMutex(instance->mutex);
  }
}

static Uint64 KotonohaTimeGetUnlocked(const Kotonoha_Time *instance) {
  if (!instance->started)
    return 0;

  if (instance->paused) {
    return (instance->pausedTicks - instance->startTicks -
            instance->accumulatedPauseTicks) +
           instance->seekTicks;
  }

  return (KotonohaTimeNow() - instance->startTicks -
          instance->accumulatedPauseTicks) +
         instance->seekTicks;
}

void KotonohaTimeReset(Kotonoha_Time *instance, bool stopAfterReset) {
  if (instance == NULL)
    return;

  KotonohaTimeLock(instance);
  instance->startTicks = stopAfterReset ? 0 : KotonohaTimeNow();
  instance->pausedTicks = 0;
  instance->accumulatedPauseTicks = 0;
  instance->seekTicks = 0;
  instance->started = !stopAfterReset;
  instance->paused = false;
  KotonohaTimeUnlock(instance);
}

Kotonoha_Time *KotonohaTimeNew(bool startStopped) {
  Kotonoha_Time *time = (Kotonoha_Time *)SDL_calloc(1, sizeof(Kotonoha_Time));
  if (time == NULL) {
    KLOG_ERROR(CORE, "Failed to allocate memory for KotonohaTime");
    return NULL;
  }

  time->mutex = SDL_CreateMutex();
  if (time->mutex == NULL) {
    KLOG_ERROR(CORE, "Failed to create KotonohaTime mutex: %s", SDL_GetError());
    SDL_free(time);
    return NULL;
  }

  KotonohaTimeReset(time, startStopped);
  return time;
}

void KotonohaTimeDestroy(Kotonoha_Time *instance) {
  if (instance != NULL) {
    if (instance->mutex != NULL) {
      SDL_DestroyMutex(instance->mutex);
      instance->mutex = NULL;
    }
    SDL_free(instance);
  }
}

void KotonohaTimeStart(Kotonoha_Time *instance) {
  if (instance == NULL)
    return;

  KotonohaTimeLock(instance);
  instance->startTicks = KotonohaTimeNow();
  instance->pausedTicks = 0;
  instance->accumulatedPauseTicks = 0;
  instance->seekTicks = 0;
  instance->started = true;
  instance->paused = false;
  KotonohaTimeUnlock(instance);
}

void KotonohaTimePause(Kotonoha_Time *instance) {
  if (instance == NULL)
    return;

  KotonohaTimeLock(instance);
  if (instance->started && !instance->paused) {
    instance->pausedTicks = KotonohaTimeNow();
    instance->paused = true;
  }
  KotonohaTimeUnlock(instance);
}

void KotonohaTimeResume(Kotonoha_Time *instance) {
  if (instance == NULL)
    return;

  KotonohaTimeLock(instance);
  if (instance->started && instance->paused) {
    instance->accumulatedPauseTicks +=
        KotonohaTimeNow() - instance->pausedTicks;
    instance->pausedTicks = 0;
    instance->paused = false;
  }
  KotonohaTimeUnlock(instance);
}

Uint64 KotonohaTimeGet(const Kotonoha_Time *instance) {
  if (instance == NULL)
    return 0;

  KotonohaTimeLock(instance);
  const Uint64 time = KotonohaTimeGetUnlocked(instance);
  KotonohaTimeUnlock(instance);
  return time;
}

void KotonohaTimeSet(Kotonoha_Time *instance, Uint64 time) {
  if (instance == NULL)
    return;

  KotonohaTimeLock(instance);
  if (!instance->started) {
    instance->startTicks = KotonohaTimeNow();
    instance->pausedTicks = 0;
    instance->accumulatedPauseTicks = 0;
    instance->seekTicks = time;
    instance->started = true;
    instance->paused = false;
    KotonohaTimeUnlock(instance);
    return;
  }

  instance->seekTicks = time;

  if (instance->paused) {
    instance->pausedTicks =
        instance->startTicks + instance->accumulatedPauseTicks;
  } else {
    instance->startTicks = KotonohaTimeNow();
    instance->accumulatedPauseTicks = 0;
  }
  KotonohaTimeUnlock(instance);
}

bool KotonohaTimeIsStarted(const Kotonoha_Time *instance) {
  if (instance == NULL)
    return false;

  KotonohaTimeLock(instance);
  const bool started = instance->started;
  KotonohaTimeUnlock(instance);
  return started;
}

bool KotonohaTimeIsPaused(const Kotonoha_Time *instance) {
  if (instance == NULL)
    return false;

  KotonohaTimeLock(instance);
  const bool paused = instance->paused;
  KotonohaTimeUnlock(instance);
  return paused;
}

void KotonohaTimeSeekForward(Kotonoha_Time *instance, Uint64 delta) {
  if (instance == NULL)
    return;

  KotonohaTimeLock(instance);
  instance->seekTicks += delta;
  KotonohaTimeUnlock(instance);
}

void KotonohaTimeSeekBackward(Kotonoha_Time *instance, Uint64 delta) {
  if (instance == NULL)
    return;

  KotonohaTimeLock(instance);
  const Uint64 current = KotonohaTimeGetUnlocked(instance);
  instance->seekTicks = current < delta ? 0 : current - delta;
  if (instance->paused) {
    instance->pausedTicks =
        instance->startTicks + instance->accumulatedPauseTicks;
  } else if (instance->started) {
    instance->startTicks = KotonohaTimeNow();
    instance->accumulatedPauseTicks = 0;
  }
  KotonohaTimeUnlock(instance);
}

Uint64 KotonohaTimeGetFromEvent(const Kotonoha_Time *instance, Uint64 start,
                                Uint64 end, bool *inRange, Sint64 *diff) {
  if (instance == NULL || inRange == NULL || diff == NULL)
    return 0;

  Uint64 currentTime = KotonohaTimeGet(instance);

  if (currentTime < start) {
    *inRange = false;
    *diff = (Sint64)currentTime - (Sint64)start;
    return 0;
  }

  if (currentTime > end) {
    *inRange = false;
    *diff = (Sint64)currentTime - (Sint64)end;
    return 0;
  }

  *inRange = true;
  *diff = 0;
  return currentTime - start;
}