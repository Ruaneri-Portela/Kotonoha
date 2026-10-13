#pragma once
#include <Kotonoha/Gameplay.hpp>

extern "C" {
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/FpsRender.h>
#include <Kotonoha/renders/TimestampRender.h>
#include <Kotonoha/renders/LoadingRender.h>
#include <Kotonoha/utils/Path.h>
#include <SDL3_ttf/SDL_ttf.h>

#if defined(__ANDROID__)
	void Kotonoha_MobileSetup();
#define KOTONOHA_MOBILE
#elif defined(__APPLE__)
#include <TargetConditionals.h>
	void Kotonoha_MobileSetup(void);
#define KOTONOHA_MOBILE
#endif
}

#include <atomic>
#include <string>
#include <tuple>
#include <vector>

namespace Kotonoha {
	class Kotonoha {
	private:
		struct Kotonoha_Game& gameContext;

		SDL_Texture* sceneTransitionFrame = nullptr;
		SDL_Thread* processPool = nullptr;
		SDL_Cursor* cursor = nullptr;
		std::atomic_bool processPoolRunning{true};
		std::vector<std::tuple<SDL_ThreadFunction, void*>> processPoolTasks;

		const char* preferredGPU = nullptr;
		std::string preferredUpscaler = "Auto";

		bool showCursor = true;
		bool fullscreen = false;
		bool enableVsync = false;
		bool ignoreMissingAssets = false;
		bool showFps = false;
		bool showTimestamp = false;
		bool showDebugUi = false;
		bool sceneTransitionPrepared = false;
		bool sceneTransitionActive = false;

		struct DeferredFile {
			std::string path;
			bool isSubtitleStyles;
		};
		std::vector<DeferredFile> deferredFiles;

		Uint64 lastMouseTime = 0;
		Uint64 lastSceneFrameCaptureTime = 0;

		size_t lastScene = static_cast<size_t>(-1);

		bool ParseArguments(int argc, char* argv[]);
		void LoadSubtitleStylesFile(const char* path);
		void LoadWindowIcon(const char* path);
		bool CaptureSceneTransitionFrame(bool activateTransition = true);

		static int EventsThread(void* data);

	public:
		std::unordered_map<std::string, TTF_Font*> fonts;
		std::vector<Gameplay*> gameplays;
		Sound::Channel* BGM = nullptr;
		Sound::Channel* Voice = nullptr;
		Sound::Channel* Se = nullptr;

		bool LoadScriptFile(const char* path);
		void DeleteGameplay(Gameplay* gameplay);
		void CacheSceneTransitionFrameIfNeeded(Gameplay* current);
		void PresentSceneTransitionFrame(Gameplay* incoming);

		void ClearGameplays();
		void ConfigureWindow();

		Kotonoha(int argc, char* argv[], SDL_AppResult* initStatus,
			struct Kotonoha_Game& gameContext);

		SDL_AppResult Initialize();

		SDL_AppResult Event(SDL_Event* event);

		SDL_AppResult Main(Gameplay** out);

		~Kotonoha();
	};
} // namespace Kotonoha

void Kotonoha_BasicGuiInit(Kotonoha_Game& gameContext, std::vector<Uint8> font);
bool Kotonoha_BasicGuiRun(Kotonoha::Kotonoha* game, Kotonoha::Gameplay* play, Kotonoha_Game& context);
void Kotonoha_BasicGuiEvent(SDL_Event* event);