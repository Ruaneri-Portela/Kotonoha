#include <Kotonoha/utils/Time.h>

static Uint64 Kotonoha_timeNow(void) { return SDL_GetTicks(); }

void Kotonoha_timeReset(Kotonoha_time* instance, bool stopAfterReset) {
	if (instance == NULL)
		return;

	instance->startTicks = stopAfterReset ? 0 : Kotonoha_timeNow();
	instance->pausedTicks = 0;
	instance->accumulatedPauseTicks = 0;
	instance->seekTicks = 0;
	instance->started = !stopAfterReset;
	instance->paused = false;
}

Kotonoha_time* Kotonoha_timeNew(bool startStopped) {
	Kotonoha_time* time = (Kotonoha_time*)SDL_malloc(sizeof(Kotonoha_time));
	if (time == NULL) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Failed to allocate memory for Kotonoha_time");
		return NULL;
	}

	Kotonoha_timeReset(time, startStopped);
	return time;
}

void Kotonoha_timeDestroy(Kotonoha_time* instance) {
	if (instance != NULL) {
		SDL_free(instance);
	}
}

void Kotonoha_timeStart(Kotonoha_time* instance) {
	if (instance == NULL)
		return;

	instance->startTicks = Kotonoha_timeNow();
	instance->pausedTicks = 0;
	instance->accumulatedPauseTicks = 0;
	instance->seekTicks = 0;
	instance->started = true;
	instance->paused = false;
}

void Kotonoha_timePause(Kotonoha_time* instance) {
	if (instance == NULL || !instance->started || instance->paused)
		return;

	instance->pausedTicks = Kotonoha_timeNow();
	instance->paused = true;
}

void Kotonoha_timeResume(Kotonoha_time* instance) {
	if (instance == NULL || !instance->started || !instance->paused)
		return;

	instance->accumulatedPauseTicks += Kotonoha_timeNow() - instance->pausedTicks;
	instance->pausedTicks = 0;
	instance->paused = false;
}

Uint64 Kotonoha_timeGet(const Kotonoha_time* instance) {
	if (instance == NULL || !instance->started)
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

void Kotonoha_timeSet(Kotonoha_time* instance, Uint64 time) {
	if (instance == NULL)
		return;

	if (!instance->started) {
		instance->startTicks = Kotonoha_timeNow();
		instance->pausedTicks = 0;
		instance->accumulatedPauseTicks = 0;
		instance->seekTicks = time;
		instance->started = true;
		instance->paused = false;
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
}

bool Kotonoha_timeIsStarted(const Kotonoha_time* instance) {
	return instance != NULL && instance->started;
}

bool Kotonoha_timeIsPaused(const Kotonoha_time* instance) {
	return instance != NULL && instance->paused;
}

void Kotonoha_timeSeekForward(Kotonoha_time* instance, Uint64 delta) {
	if (instance == NULL)
		return;

	instance->seekTicks += delta;
}

void Kotonoha_timeSeekBackward(Kotonoha_time* instance, Uint64 delta) {
	if (instance == NULL)
		return;

	Uint64 current = Kotonoha_timeGet(instance);
	instance->seekTicks -= (current < delta ? current : delta);
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