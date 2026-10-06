#pragma once

#include <vector>

extern "C" {
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
}

namespace Kotonoha {

enum class FadeColor : Uint8 {
	Black,
	White
};

enum class FadeDirection : Uint8 {
	In,
	Out
};

struct SchoolDaysFadeEvent {
	Uint64 startTick = 0;
	Uint64 endTick = 0;
	FadeColor color = FadeColor::Black;
	FadeDirection direction = FadeDirection::In;
	bool hasLoggedTick = false;
	Uint64 lastLoggedTick = 0;
};

class Fade {
private:
	Kotonoha_time* timeManager = nullptr;
	SDL_Mutex* lock = nullptr;
	std::vector<SchoolDaysFadeEvent> events;

	static bool DecodeOrsTick(Uint64 packed, bool allowParserNudge, Uint64* tick);
	static Uint64 CurrentTick(Uint64 timeMs);

public:
	explicit Fade(Kotonoha_time* time);

	void Register(Uint64 startPacked,
		Uint64 endPacked,
		FadeColor color,
		FadeDirection direction);

	void Reset();

	static Uint8 AlphaAtTick(Uint64 tick,
		Uint64 startTick,
		Uint64 endTick,
		FadeDirection direction);

	static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

	~Fade();
};

} // namespace Kotonoha
