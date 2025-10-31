#pragma once
#include <Kotonoha/Gameplay.hpp>

extern "C"
{
#include <SDL3_ttf/SDL_ttf.h>
#include <Kotonoha/renders/FPSrender.h>
#include <Kotonoha/renders/AudioRender.h>
}

#include <tuple>

namespace Kotonoha
{
	class Kotonoha
	{
	private:
		struct Kotonoha_Game gameContext = {0};
		std::vector<Gameplay *> gameplays;

		SDL_Thread *processPool = nullptr;
		SDL_Cursor *cursor = nullptr;
		bool processPoolRunning = true;
		std::vector<std::tuple<SDL_ThreadFunction, void *>> processPoolTasks;

		Sound sound;
		Sound::Channel *BGM = nullptr;
		Sound::Channel *Voice = nullptr;
		Sound::Channel *Se = nullptr;

		char *preferedGPU = nullptr;
		int windowsWidth = 1280;
		int windowsHeight = 720;

		bool showCursor;
		Uint64 lastMouseTime;

		bool parserArguments(int argc, char *argv[], bool initDependent);
		void loadSubtitleStylesFile(char *path);
		void loadWindowIcon(const char *path);

		static int EventsThread(void *data);

	public:
		Kotonoha(int argc, char *argv[], SDL_AppResult *initStatus);

		SDL_AppResult Event(SDL_Event *event);

		SDL_AppResult Main();

		~Kotonoha();
	};
}