#include <Kotonoha/components/Fade.hpp>

namespace Kotonoha {

namespace {
	const char* FadeColorName(FadeColor color) {
		return color == FadeColor::White ? "White" : "Black";
	}

	const char* FadeDirectionName(FadeDirection direction) {
		return direction == FadeDirection::In ? "IN" : "OUT";
	}
}

Fade::Fade(Kotonoha_time* time)
	: timeManager(time), lock(SDL_CreateMutex()) {
	if (lock == nullptr) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"[FADE] Failed to create fade mutex: %s",
			SDL_GetError());
	}
}

bool Fade::DecodeOrsTick(Uint64 packed, bool allowParserNudge, Uint64* tick) {
	if (tick == nullptr) {
		return false;
	}

	auto decodeCandidate = [](Uint64 value, Uint64* outTick) -> bool {
		const Uint64 minutes = value / 60000;
		const Uint64 rem = value % 60000;
		const Uint64 seconds = rem / 1000;
		const Uint64 tail = rem % 1000;

		if (seconds >= 60 || (tail % 10) != 0) {
			return false;
		}

		const Uint64 frameField = tail / 10;
		if (frameField >= 24) {
			return false;
		}

		const Uint64 zeroBased =
			(minutes * 60 + seconds) * 24 + frameField;
		*outTick = zeroBased + 1;
		return true;
	};

	if (decodeCandidate(packed, tick)) {
		return true;
	}

	return allowParserNudge && packed > 0 &&
		decodeCandidate(packed - 1, tick);
}

Uint64 Fade::CurrentTick(Uint64 timeMs) {
	return ((timeMs * 24) / 1000) + 1;
}

Uint8 Fade::AlphaAtTick(Uint64 tick,
	Uint64 startTick,
	Uint64 endTick,
	FadeDirection direction) {
	if (endTick <= startTick) {
		return direction == FadeDirection::In ? 0 : 255;
	}

	Uint64 clampedTick = tick;
	if (clampedTick < startTick) {
		clampedTick = startTick;
	}
	else if (clampedTick > endTick) {
		clampedTick = endTick;
	}

	const Uint64 elapsed = clampedTick - startTick;
	const Uint64 duration = endTick - startTick;
	const Uint64 q = (255 * elapsed) / duration;

	return static_cast<Uint8>(
		direction == FadeDirection::In ? 255 - q : q);
}

void Fade::Register(Uint64 startPacked,
	Uint64 endPacked,
	FadeColor color,
	FadeDirection direction) {
	if (lock == nullptr) {
		return;
	}

	Uint64 startTick = 0;
	Uint64 endTick = 0;
	if (!DecodeOrsTick(startPacked, true, &startTick) ||
		!DecodeOrsTick(endPacked, false, &endTick) ||
		endTick < startTick) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"[SD-FADE] Invalid ORS fade range start=%llu end=%llu",
			static_cast<unsigned long long>(startPacked),
			static_cast<unsigned long long>(endPacked));
		return;
	}

	SDL_LockMutex(lock);
	for (const auto& existing : events) {
		if (existing.startTick == startTick &&
			existing.endTick == endTick &&
			existing.color == color &&
			existing.direction == direction) {
			SDL_UnlockMutex(lock);
			return;
		}
	}

	FadeEvent event{};
	event.startTick = startTick;
	event.endTick = endTick;
	event.color = color;
	event.direction = direction;
	events.push_back(event);
	SDL_UnlockMutex(lock);

	SDL_Log("[SD-FADE] register %s %s ticks=%llu..%llu",
		FadeColorName(color),
		FadeDirectionName(direction),
		static_cast<unsigned long long>(startTick),
		static_cast<unsigned long long>(endTick));
}

void Fade::Reset() {
	if (lock == nullptr) {
		events.clear();
		return;
	}

	SDL_LockMutex(lock);
	events.clear();
	SDL_UnlockMutex(lock);
}

Kotonoha_Scene_Status Fade::Render(KOTONOHA_SCENE_CALL) {
	(void)window;
	(void)eventQueu;
	(void)target;

	auto* here = static_cast<Fade*>(userData);
	if (here == nullptr || render == nullptr ||
		here->timeManager == nullptr || here->lock == nullptr) {
		return KOTONOHA_SCENE_NULL;
	}

	const Uint64 tick = CurrentTick(Kotonoha_timeGet(here->timeManager));

	SDL_LockMutex(here->lock);
	FadeEvent* active = nullptr;
	int activeCount = 0;

	for (auto& event : here->events) {
		if (tick < event.startTick || tick > event.endTick) {
			continue;
		}

		++activeCount;
		if (active == nullptr || event.startTick >= active->startTick) {
			active = &event;
		}
	}

	if (active == nullptr) {
		SDL_UnlockMutex(here->lock);
		return KOTONOHA_SCENE_WAITING;
	}

	if (activeCount > 1) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"[SD-FADE] overlapping fades=%d at tick=%llu; latest start wins",
			activeCount,
			static_cast<unsigned long long>(tick));
	}

	const Uint8 alpha = AlphaAtTick(
		tick, active->startTick, active->endTick, active->direction);
	const Uint8 channel = active->color == FadeColor::White ? 255 : 0;

	if (!active->hasLoggedTick || active->lastLoggedTick != tick) {
		SDL_Log("[FADE] %s %s tick=%llu start=%llu end=%llu alpha=%u",
			FadeColorName(active->color),
			FadeDirectionName(active->direction),
			static_cast<unsigned long long>(tick),
			static_cast<unsigned long long>(active->startTick),
			static_cast<unsigned long long>(active->endTick),
			static_cast<unsigned int>(alpha));
		active->hasLoggedTick = true;
		active->lastLoggedTick = tick;
	}

	SDL_UnlockMutex(here->lock);

	SDL_SetRenderDrawColor(render, channel, channel, channel, alpha);
	SDL_RenderClear(render);
	return KOTONOHA_SCENE_DRAW;
}

Fade::~Fade() {
	Reset();
	if (lock != nullptr) {
		SDL_DestroyMutex(lock);
		lock = nullptr;
	}
}

} // namespace Kotonoha
