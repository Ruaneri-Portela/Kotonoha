#pragma once

#include <SDL3/SDL.h>

static inline bool Kotonoha_OrsTimeToTick(Uint64 packed,
	bool allowParserNudge, Uint64* tick) {
	if (tick == NULL) {
		return false;
	}

	const Uint64 max = SDL_MAX_UINT64;
	for (int attempt = 0; attempt <= (allowParserNudge ? 1 : 0); ++attempt) {
		if (attempt != 0 && packed == 0) {
			break;
		}

		const Uint64 value = packed - (Uint64)attempt;
		const Uint64 minutes = value / 60000;
		const Uint64 remainder = value % 60000;
		const Uint64 seconds = remainder / 1000;
		const Uint64 framePart = remainder % 1000;
		if (seconds >= 60 || framePart % 10 != 0 || framePart / 10 >= 24) {
			continue;
		}

		if (minutes > (max / 24 - seconds) / 60) {
			continue;
		}
		const Uint64 zeroBased = (minutes * 60 + seconds) * 24 +
			framePart / 10;
		if (zeroBased == max) {
			continue;
		}

		*tick = zeroBased + 1;
		return true;
	}

	return false;
}

static inline bool Kotonoha_OrsTimeToMilliseconds(Uint64 packed,
	bool allowParserNudge, Uint64* milliseconds) {
	if (milliseconds == NULL) {
		return false;
	}

	Uint64 tick = 0;
	if (!Kotonoha_OrsTimeToTick(packed, allowParserNudge, &tick)) {
		return false;
	}

	const Uint64 zeroBased = tick - 1;
	const Uint64 wholeSeconds = zeroBased / 24;
	const Uint64 remainingTicks = zeroBased % 24;
	if (wholeSeconds > SDL_MAX_UINT64 / 1000) {
		return false;
	}
	*milliseconds = wholeSeconds * 1000 +
		(remainingTicks * 1000 + 23) / 24;
	return true;
}

static inline Uint64 Kotonoha_MillisecondsToOrsTick(Uint64 milliseconds) {
	const Uint64 wholeSeconds = milliseconds / 1000;
	const Uint64 remainingMs = milliseconds % 1000;
	if (wholeSeconds > (SDL_MAX_UINT64 - 24) / 24) {
		return SDL_MAX_UINT64;
	}
	return wholeSeconds * 24 + (remainingMs * 24) / 1000 + 1;
}

static inline bool Kotonoha_OrsTimeToZeroBasedFrame(Uint64 packed,
	bool allowParserNudge, Uint64* frame) {
	Uint64 tick = 0;
	if (frame == NULL ||
		!Kotonoha_OrsTimeToTick(packed, allowParserNudge, &tick)) {
		return false;
	}
	*frame = tick - 1;
	return true;
}
