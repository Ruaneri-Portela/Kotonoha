#include <cstring>
#include <Kotonoha/Kotonoha.hpp>

static struct Kotonoha_Game global = {};

namespace {

	static void CleanupGlobalResources() {
		Kotonoha_FPSRenderShutdown();
		Kotonoha_TimestampRenderShutdown();
		Kotonoha_LoadingRenderShutdown();

		delete static_cast<Kotonoha::Sound*>(global.sound);
		global.sound = nullptr;

		if (global.ass_track != nullptr) {
			ass_free_track(global.ass_track);
			global.ass_track = nullptr;
		}

		if (global.ass_renderer != nullptr) {
			ass_renderer_done(global.ass_renderer);
			global.ass_renderer = nullptr;
		}

		if (global.ass_library != nullptr) {
			ass_library_done(global.ass_library);
			global.ass_library = nullptr;
		}

		if (global.window != nullptr) {
			SDL_DestroyWindow(global.window);
			global.window = nullptr;
		}
	}

	static bool InitSDLSubsystems() {
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
			SDL_LogError(0, "Couldn't initialize SDL: %s", SDL_GetError());
			return false;
		}

		if (!TTF_Init()) {
			SDL_LogError(0, "Couldn't initialize TTF: %s", SDL_GetError());
			SDL_Quit();
			return false;
		}

		return true;
	}

	static bool InitWindow() {
		int width = 1280;
		int height = 720;
		SDL_WindowFlags windowFlags =
			global.flags | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;

		global.window = SDL_CreateWindow(
			"Kotonoha Engine",
			width,
			height,
			windowFlags);

		if (global.window == nullptr) {
			SDL_LogError(0, "Couldn't create window: %s", SDL_GetError());
			return false;
		}
		return true;
	}

	void PreloadFontsIntoAssLibrary(ASS_Library *library, const char *fontsDir) {
		if (library == nullptr || fontsDir == nullptr) return;

		SDL_PathInfo info;
		if (!SDL_GetPathInfo(fontsDir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
			return;
		}

		struct ScanContext {
			ASS_Library *lib;
			std::string dir;
		} ctx{ library, fontsDir };

		SDL_EnumerateDirectory(fontsDir, [](void *userdata, const char *, const char *entry) {
			auto *c = static_cast<ScanContext*>(userdata);
			std::string name(entry);
			if (name.length() > 4) {
				std::string ext = name.substr(name.length() - 4);
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".ttf" || ext == ".otf" || ext == ".ttc") {
					std::string fullPath = c->dir + "/" + name;
					size_t size = 0;
					void *data = SDL_LoadFile(fullPath.c_str(), &size);
					if (data != nullptr && size > 0) {
						ass_add_font(c->lib, const_cast<char*>(name.c_str()), static_cast<char*>(data), static_cast<int>(size));
						SDL_free(data);
						SDL_Log("Preloaded font into LibASS memory: %s (%zu bytes)", name.c_str(), size);
					}
				}
			}
			return SDL_ENUM_CONTINUE;
		}, &ctx);
	}

	static bool InitLibAss() {
		global.ass_library = ass_library_init();
		if (global.ass_library == nullptr) {
			SDL_LogError(0, "Couldn't initialize libAss");
			return false;
		}

		global.ass_renderer = ass_renderer_init(global.ass_library);
		if (global.ass_renderer == nullptr) {
			SDL_LogError(0, "Couldn't initialize libAss Render");
			return false;
		}

		// Precarrega todas as fontes de assets/fonts na memória do LibASS para evitar acessos repetidos de disco
		PreloadFontsIntoAssLibrary(global.ass_library, "assets/fonts");

		ass_set_extract_fonts(global.ass_library, 1);
		ass_set_fonts_dir(global.ass_library, "assets/fonts");
		ass_set_fonts(global.ass_renderer,
			"NotoSans-Regular.ttf",
			nullptr,
			ASS_FONTPROVIDER_AUTODETECT,
			nullptr,
			1);
		ass_set_shaper(global.ass_renderer, ASS_SHAPING_COMPLEX);
		ass_set_hinting(global.ass_renderer, ASS_HINTING_NATIVE);

		return true;
	}

	static bool InitSound() {
		global.sound = new (std::nothrow) Kotonoha::Sound;
		if (global.sound == nullptr) {
			SDL_LogError(0, "Couldn't create sound engine");
			return false;
		}

		return true;
	}

	static Kotonoha::Kotonoha* GetApp(void* appstate) {
		return static_cast<Kotonoha::Kotonoha*>(appstate);
	}

} // namespace

extern "C" {
#define SDL_MAIN_USE_CALLBACKS 1
#include <Kotonoha/speciais/Upscaling.h>
#include <SDL3/SDL_main.h>
	SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
		#if defined(KOTONOHA_MOBILE)
			Kotonoha_MobileSetup();
		#endif

		if (appstate == nullptr) {
			return SDL_APP_FAILURE;
		}

		*appstate = nullptr;
		std::memset(&global, 0, sizeof(global));

		global.videoWidth = 1280;
		global.videoHeight = 720;

		SDL_AppResult status = SDL_APP_FAILURE;
		Kotonoha::Kotonoha* engine =
			new (std::nothrow) Kotonoha::Kotonoha(argc, argv, &status, global);

		if (engine == nullptr) {
			SDL_LogError(0, "Couldn't create Kotonoha engine instance");
			return SDL_APP_FAILURE;
		}

		if (status != SDL_APP_CONTINUE) {
			delete engine;
			return status;
		}

		if (!InitSDLSubsystems()) {
			delete engine;
			return SDL_APP_FAILURE;
		}

		if (!InitWindow()) {
			delete engine;
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}
		engine->ConfigureWindow();

		if (!InitLibAss()) {
			delete engine;
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}

		if (!InitSound()) {
			delete engine;
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}

		status = engine->Initialize();

		if (status != SDL_APP_CONTINUE) {
			delete engine;
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
			return status;
		}

		*appstate = engine;
		if (global.render != nullptr) {
			Kotonoha_BasicGuiInit(global);
			SDL_ShowWindow(global.window);
		}

		Kotonoha_UpscalingInit(global.render, "Auto", Kotonoha_UpscalingFactor());

		return status;
	}

	SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
		if (event == nullptr) {
			return SDL_APP_CONTINUE;
		}

		Kotonoha_BasicGuiEvent(event);

		Kotonoha::Kotonoha* app = GetApp(appstate);
		if (event->type == SDL_EVENT_QUIT) {
			return SDL_APP_SUCCESS;
		}
		if (app == nullptr) {
			return SDL_APP_CONTINUE;
		}
		
		return app->Event(event);
	}

	SDL_AppResult SDL_AppIterate(void* appstate) {
		Kotonoha::Kotonoha* app = GetApp(appstate);
		if (app == nullptr || global.render == nullptr) {
			return SDL_APP_FAILURE;
		}

		Kotonoha::Gameplay* inRunning = nullptr;
		SDL_SetRenderDrawColor(global.render, 0, 0, 0, 0);
		SDL_RenderClear(global.render);

		const SDL_AppResult status = app->Main(&inRunning);
		if (status != SDL_APP_CONTINUE) {
			return status;
		}

		if(inRunning == nullptr) {
			Kotonoha_eventFree(&global.eventQueu);
		}

		app->CacheSceneTransitionFrameIfNeeded(inRunning);
		app->PresentSceneTransitionFrame(inRunning);
		if (global.showFps) {
			Kotonoha_FPSRender(global.window, global.render, nullptr, nullptr, nullptr);
		}
		if (global.showTimestamp && inRunning != nullptr) {
			Kotonoha_TimestampRender(global.window, global.render, nullptr, inRunning->tm, nullptr);
		}
		if (inRunning != nullptr &&
			Kotonoha_IOMonitorIsLoading(&inRunning->ioMonitor)) {
			Kotonoha_LoadingRender(
				global.window, global.render, nullptr, nullptr, nullptr);
		}
		if (!Kotonoha_BasicGuiRun(app, inRunning, global)) {
			return SDL_APP_SUCCESS;
		}
		SDL_RenderPresent(global.render);
		return SDL_APP_CONTINUE;
	}

	void SDL_AppQuit(void* appstate, SDL_AppResult result) {
		(void)result;

		Kotonoha_UpscalingShutdown();

		delete GetApp(appstate);
		CleanupGlobalResources();
		TTF_Quit();
		SDL_Quit();
	}
}
