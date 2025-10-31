#pragma once
#include <SDL3/SDL.h>

struct Kotonoha_time
{
	bool isPaused;
	Uint64 startTime;

	Uint64 pauseTime;
	Uint64 pauseTimeStart;

	Uint64 pauseDuration;
	Sint64 seekTime;
};

struct Kotonoha_time* Kotonoha_timeNew(bool startStoped);

void Kotonoha_timePause(struct Kotonoha_time* instance);

void Kotonoha_timeResume(struct Kotonoha_time* instance);

void Kotonoha_timeReset(struct Kotonoha_time* instance, bool resetStoped);

void Kotonoha_timeSet(struct Kotonoha_time* instance, Uint64 time);

Uint64 Kotonoha_timeGet(struct Kotonoha_time* instance);

Uint64 Kotonoha_timeGetFromEvent(struct Kotonoha_time* instance, Uint64 start, Uint64 end, bool* inRange, int* diff);

void Kotonoha_timeDestroy(struct Kotonoha_time* instance);