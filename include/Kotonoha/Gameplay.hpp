#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/components/Fade.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/components/Video.hpp>
#include <Kotonoha/utils/IOMonitor.h>

extern "C" {
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/utils/Time.h>
}

namespace Kotonoha {
	class Gameplay {
	private:
		float aspectRatio = 16.0f / 9.0f;
		int windowWidth = 0, windowHeight = 0;
		bool lastPauseStatus = false, playOnlyOnFocus = false;
		bool loadingPauseActive = false, pausedBeforeLoading = false;

		float deltaTime = 0.0f;
		Uint64 lastFrameNs = 0;
		
		void UpdateDeltaTime();
				
		void UpdateCanvasSize(SDL_Window* window, SDL_Renderer* renderer);
		void UpdateLoadingPause(struct Kotonoha_Game* gameContext);
	
		struct TriggerSeekState {
			Sint16 value = 0;
			bool active = false;
		};

		// dentro da classe Gameplay:
		TriggerSeekState leftTrigger;
		TriggerSeekState rightTrigger;

		void SetLeftTrigger(Sint16 value);
		void SetRightTrigger(Sint16 value);

		void UpdateTriggers(float deltaTimeSeconds);

		void HandleTriggerSeek(
			TriggerSeekState& state,
			float deltaTimeSeconds,
			bool backward
		);

		void HandleGamepadButton(SDL_Event* event, Kotonoha_Game* gameContext);
		void HandleGamepadAxis(SDL_Event* event, Kotonoha_Game* gameContext);
		void HandleKeyboard(SDL_Event* event, Kotonoha_Game* gameContext);
		void HandleWindowEvent(SDL_Event* event,Kotonoha_Game* gameContext);

		void SeekByKeyboardN(Kotonoha_Game* gameContext);
		void SeekByKeyboardB(Kotonoha_Game* gameContext);
#ifdef Kotonoha_MobileSetup
		void HandleMobileFocus(SDL_Event* event, Kotonoha_Game* gameContext);
#endif

	public:
		struct Kotonoha_subtitles* sb = nullptr;
		struct Kotonoha_time* tm = nullptr;
		Kotonoha_IOMonitor ioMonitor{};
		Canvas* drawCanvas = nullptr;
		Event* eventManager = nullptr;
		Video* video = nullptr;
		Image* image = nullptr;
		Audio* audio = nullptr;
		Fade* fade = nullptr;
		Prompt* prompt = nullptr;
		int promptId = -1;
		bool putPrompt = false;

		bool reset = false, back = false, hardReset = false, loop = false, firstFocus = true;

		std::string scriptPath;

		Gameplay(const char* scriptPath, struct Kotonoha_Game* gameContext);
		SDL_AppResult Main(struct Kotonoha_Game* gameContext);

		void Events(SDL_Event* event,struct Kotonoha_Game* gameContext);
		void Reset(bool resetTime);

		void Pause();
		void Resume();

		void SeekForward(Uint64 ms);
		void SeekBackward(Uint64 ms);

		float GetDeltaTime() const;
		float GetTime();
		float GetLastTime();
		void SetTime(float time);

		~Gameplay();
	};
} // namespace Kotonoha
