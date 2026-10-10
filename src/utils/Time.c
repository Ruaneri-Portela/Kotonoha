#include <Kotonoha/utils/Time.h>

static Uint64 Kotonoha_timeNow(void) { return SDL_GetTicks(); }

static void Kotonoha_timeLock(const Kotonoha_time* instance) {
	if (instance != NULL && instance->mutex != NULL) {
		SDL_LockMutex(instance->mutex);
	}
}

static void Kotonoha_timeUnlock(const Kotonoha_time* instance) {
	if (instance != NULL && instance->mutex != NULL) {
		SDL_UnlockMutex(instance->mutex);
	}
}

static Uint64 Kotonoha_timeGetUnlocked(const Kotonoha_time* instance) {
	if (!instance->started)
		return 0;

	if (instance->paused) {
		return (instance->pausedTicks - instance->startTicks -
			instance->accumulatedPauseTicks) +
			instance->seekTicks;
	}

	return (Kotonoha_timeNow() - instance->startTicks -
		instance->accumulatedPauseTicks) +
		instance->seekTicks;
}

void Kotonoha_timeReset(Kotonoha_time* instance, bool stopAfterReset) {
	if (instance == NULL)
		return;

	Kotonoha_timeLock(instance);
	instance->startTicks = stopAfterReset ? 0 : Kotonoha_timeNow();
	instance->pausedTicks = 0;
	instance->accumulatedPauseTicks = 0;
	instance->seekTicks = 0;
	instance->started = !stopAfterReset;
	instance->paused = false;
	Kotonoha_timeUnlock(instance);
}

Kotonoha_time* Kotonoha_timeNew(bool startStopped) {
	Kotonoha_time* time = (Kotonoha_time*)SDL_calloc(1, sizeof(Kotonoha_time));
	if (time == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Failed to allocate memory for Kotonoha_time");
		return NULL;
	}

	time->mutex = SDL_CreateMutex();
	if (time->mutex == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Failed to create Kotonoha_time mutex: %s", SDL_GetError());
		SDL_free(time);
		return NULL;
	}

	Kotonoha_timeReset(time, startStopped);
	return time;
}

void Kotonoha_timeDestroy(Kotonoha_time* instance) {
	if (instance != NULL) {
		if (instance->mutex != NULL) {
			SDL_DestroyMutex(instance->mutex);
			instance->mutex = NULL;
		}
		SDL_free(instance);
	}
}

void Kotonoha_timeStart(Kotonoha_time* instance) {
	if (instance == NULL)
		return;

	Kotonoha_timeLock(instance);
	instance->startTicks = Kotonoha_timeNow();
	instance->pausedTicks = 0;
	instance->accumulatedPauseTicks = 0;
	instance->seekTicks = 0;
	instance->started = true;
	instance->paused = false;
	Kotonoha_timeUnlock(instance);
}

void Kotonoha_timePause(Kotonoha_time* instance) {
	if (instance == NULL)
		return;

	Kotonoha_timeLock(instance);
	if (instance->started && !instance->paused) {
		instance->pausedTicks = Kotonoha_timeNow();
		instance->paused = true;
	}
	Kotonoha_timeUnlock(instance);
}

void Kotonoha_timeResume(Kotonoha_time* instance) {
	if (instance == NULL)
		return;

	Kotonoha_timeLock(instance);
	if (instance->started && instance->paused) {
		instance->accumulatedPauseTicks +=
			Kotonoha_timeNow() - instance->pausedTicks;
		instance->pausedTicks = 0;
		instance->paused = false;
	}
	Kotonoha_timeUnlock(instance);
}

Uint64 Kotonoha_timeGet(const Kotonoha_time* instance) {
	if (instance == NULL)
		return 0;

	Kotonoha_timeLock(instance);
	const Uint64 time = Kotonoha_timeGetUnlocked(instance);
	Kotonoha_timeUnlock(instance);
	return time;
}

void Kotonoha_timeSet(Kotonoha_time* instance, Uint64 time) {
	if (instance == NULL)
		return;

	Kotonoha_timeLock(instance);
	if (!instance->started) {
		instance->startTicks = Kotonoha_timeNow();
		instance->pausedTicks = 0;
		instance->accumulatedPauseTicks = 0;
		instance->seekTicks = time;
		instance->started = true;
		instance->paused = false;
		Kotonoha_timeUnlock(instance);
		return;
	}

	instance->seekTicks = time;

	if (instance->paused) {
		instance->pausedTicks =
			instance->startTicks + instance->accumulatedPauseTicks;
	}
	else {
		instance->startTicks = Kotonoha_timeNow();
		instance->accumulatedPauseTicks = 0;
	}
	Kotonoha_timeUnlock(instance);
}

bool Kotonoha_timeIsStarted(const Kotonoha_time* instance) {
	if (instance == NULL)
		return false;

	Kotonoha_timeLock(instance);
	const bool started = instance->started;
	Kotonoha_timeUnlock(instance);
	return started;
}

bool Kotonoha_timeIsPaused(const Kotonoha_time* instance) {
	if (instance == NULL)
		return false;

	Kotonoha_timeLock(instance);
	const bool paused = instance->paused;
	Kotonoha_timeUnlock(instance);
	return paused;
}

void Kotonoha_timeSeekForward(Kotonoha_time* instance, Uint64 delta) {
	if (instance == NULL)
		return;

	Kotonoha_timeLock(instance);
	instance->seekTicks += delta;
	Kotonoha_timeUnlock(instance);
}

void Kotonoha_timeSeekBackward(Kotonoha_time* instance, Uint64 delta) {
	if (instance == NULL)
		return;

	Kotonoha_timeLock(instance);
	const Uint64 current = Kotonoha_timeGetUnlocked(instance);
	instance->seekTicks = current < delta ? 0 : current - delta;
	if (instance->paused) {
		instance->pausedTicks =
			instance->startTicks + instance->accumulatedPauseTicks;
	}
	else if (instance->started) {
		instance->startTicks = Kotonoha_timeNow();
		instance->accumulatedPauseTicks = 0;
	}
	Kotonoha_timeUnlock(instance);
}

Uint64 Kotonoha_timeGetFromEvent(const Kotonoha_time* instance, Uint64 start,
	Uint64 end, bool* inRange, Sint64* diff) {
	if (instance == NULL || inRange == NULL || diff == NULL)
		return 0;

	Uint64 currentTime = Kotonoha_timeGet(instance);

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