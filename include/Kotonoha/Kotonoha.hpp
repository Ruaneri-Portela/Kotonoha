#pragma once
#include "Kotonoha/components/Sound.hpp"
#include <Kotonoha/Gameplay.hpp>

extern "C" {
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/FPSRender.h>
#include <Kotonoha/renders/TimestampRender.h>
#include <SDL3_ttf/SDL_ttf.h>

#if defined(__ANDROID__)
	void Kotonoha_MobileSetup();
#define KOTONOHA_MOBILE
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS || TARGET_OS_IPHONE
	void Kotonoha_MobileSetup(void);
#define KOTONOHA_MOBILE
#endif
#endif
}

#include <tuple>

namespace Kotonoha {
	class Kotonoha {
	private:
		struct Kotonoha_Game& gameContext;

		SDL_Thread* processPool = nullptr;
		SDL_Cursor* cursor = nullptr;
		bool processPoolRunning = true;
		std::vector<std::tuple<SDL_ThreadFunction, void*>> processPoolTasks;
		char* preferedGPU = nullptr;
		int windowsWidth = 1280;
		int windowsHeight = 720;
		bool showCursor = true;
		Uint64 lastMouseTime = 0;

		bool ParseArguments(int argc, char* argv[], bool initDependent);
		void LoadSubtitleStylesFile(char* path);
		void LoadWindowIcon(const char* path);
		static int EventsThread(void* data);

	public:
		std::vector<Gameplay*> gameplays;
		Sound::Channel* BGM = nullptr;
		Sound::Channel* Voice = nullptr;
		Sound::Channel* Se = nullptr;

		bool LoadScriptFile(const char* path);
		void DeleteGameplay(Gameplay* gameplay);
		void ClearGameplays();

		Kotonoha(int argc, char* argv[], SDL_AppResult* initStatus,
			struct Kotonoha_Game& gameContext);

		SDL_AppResult Event(SDL_Event* event);

		SDL_AppResult Main(Gameplay** out);

		~Kotonoha();
	};
} // namespace Kotonoha

void Kotonoha_BasicGuiInit(Kotonoha_Game& gameContext);
bool Kotonoha_BasicGuiRun(Kotonoha::Kotonoha* game, Kotonoha::Gameplay* play, Kotonoha_Game& context);
void Kotonoha_BasicGuiEvent(SDL_Event* event);

extern bool Kotonoha_BasicGuiShow;
extern bool Kotonoha_BasicGuiEditorShow;