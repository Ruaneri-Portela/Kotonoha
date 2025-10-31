#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/components/Video.hpp>
#include <limits>
#include <vector>

extern "C"
{
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/utils/Time.h>
}

namespace Kotonoha
{
	class Gameplay
	{
	private:
		Canvas* drawCanvas = nullptr;
		Event* eventManager = nullptr;
		float aspectRatio = 16.0f / 9.0f;
		int windowWidth = 0, windowHeight = 0;
		bool lastPauseStatus = false, playOnlyOnFocus = false;

		void UpdateCanvasSize(SDL_Window* window);

	public:
		struct Kotonoha_textData* sb = nullptr;
		struct Kotonoha_time* tm = nullptr;
		Video* video = nullptr;
		Image* image = nullptr;
		Audio* audio = nullptr;
		Prompt* prompt = nullptr;
		int promptId = -1;
		bool putPrompt = false;

		Gameplay(const char* scriptPath, struct Kotonoha_Game* gameContext);
		SDL_AppResult Main(struct Kotonoha_Game* gameContext);
		~Gameplay();
	};
}