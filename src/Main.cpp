#include <cstring>
#include <memory>
#include <string>
#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfAppController.hpp>

static struct Kotonoha_Game global = {};

namespace {

	struct KtrfLaunchOptions {
		bool requested = false;
		bool valid = true;
		std::string routePath;
		std::string orsRoot;
	};

	static std::unique_ptr<Kotonoha::SchoolDaysKtrfAppController> ktrfController;

	static KtrfLaunchOptions ExtractKtrfLaunchOptions(int argc, char* argv[]) {
		KtrfLaunchOptions options;

		for (int i = 1; i < argc; ++i) {
			if (argv[i] == nullptr || std::strcmp(argv[i], "-K") != 0) continue;

			if (options.requested || i + 2 >= argc || argv[i + 1] == nullptr ||
				argv[i + 2] == nullptr) {
				options.valid = false;
				return options;
			}

			options.requested = true;
			options.routePath = argv[i + 1];
			options.orsRoot = argv[i + 2];
			*argv[i] = '!';
			i += 2;
		}

		if (options.requested) {
			for (int i = 1; i < argc; ++i) {
				if (argv[i] != nullptr && std::strcmp(argv[i], "-l") == 0) {
					options.valid = false;
					return options;
				}
			}
		}

		return options;
	}

	static void CleanupGlobalResources() {
		delete static_cast<Kotonoha::Sound*>(global.sound);
		global.sound = nullptr;

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
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
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
		global.window = SDL_CreateWindow(
			"Kotonoha Engine",
			1280,
			720,
			global.flags | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);

		if (global.window == nullptr) {
			SDL_LogError(0, "Couldn't create window: %s", SDL_GetError());
			return false;
		}
		return true;
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

		ass_set_extract_fonts(global.ass_library, 1);
		ass_set_fonts_dir(global.ass_library, "assets/fonts");
		ass_set_fonts(global.ass_renderer,
			"assets/fonts/NotoSans-Regular.ttf",
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
#include <SDL3/SDL_main.h>

	SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
		if (appstate == nullptr) {
			return SDL_APP_FAILURE;
		}

		*appstate = nullptr;
		ktrfController.reset();
		std::memset(&global, 0, sizeof(global));

#if defined(KOTONOHA_MOBILE)
		Kotonoha_MobileSetup();
#endif

		if (!InitSDLSubsystems()) {
			return SDL_APP_FAILURE;
		}

		if (!InitWindow()) {
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}

		if (!InitLibAss()) {
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}

		if (!InitSound()) {
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}

		const KtrfLaunchOptions ktrfOptions = ExtractKtrfLaunchOptions(argc, argv);
		if (!ktrfOptions.valid) {
			SDL_LogError(0,
				"Invalid KTRF launch arguments. Use -K <route.ktnroute> <ors-root> and do not combine it with -l.");
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}

		SDL_AppResult status = SDL_APP_FAILURE;
		Kotonoha::Kotonoha* engine =
			new (std::nothrow) Kotonoha::Kotonoha(argc, argv, &status, global);

		if (engine == nullptr) {
			SDL_LogError(0, "Couldn't create Kotonoha engine instance");
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
			return SDL_APP_FAILURE;
		}

		if (status == SDL_APP_CONTINUE && ktrfOptions.requested) {
			ktrfController = std::make_unique<Kotonoha::SchoolDaysKtrfAppController>();
			Kotonoha_KtrfError error{};
			if (!ktrfController->Open(ktrfOptions.routePath.c_str(),
					ktrfOptions.orsRoot.c_str(), engine, &global, &error)) {
				SDL_LogError(0, "Couldn't start School Days KTRF mode: %s", error.message);
				ktrfController.reset();
				delete engine;
				CleanupGlobalResources();
				TTF_Quit();
				SDL_Quit();
				return SDL_APP_FAILURE;
			}
		}

		*appstate = engine;

		if (status == SDL_APP_CONTINUE && global.render != nullptr) {
			Kotonoha_BasicGuiInit(global);
		}

		if (status != SDL_APP_CONTINUE) {
			ktrfController.reset();
			delete engine;
			*appstate = nullptr;
			CleanupGlobalResources();
			TTF_Quit();
			SDL_Quit();
		}

		return status;
	}

	SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
		if (event == nullptr) {
			return SDL_APP_CONTINUE;
		}

#ifdef KOTONOHA_DEV_CHECKPOINTS
		if (ktrfController != nullptr &&
			event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_F5 &&
			ktrfController->HasPendingHandoff()) {
			Kotonoha_KtrfError error{};
			if (!ktrfController->ContinueHandoff(&error) && error.message[0] != '\0') {
				SDL_LogError(0, "[KTRF-HANDOFF] continue failed: %s", error.message);
			}
			return SDL_APP_CONTINUE;
		}
#endif

		Kotonoha_BasicGuiEvent(event);

		Kotonoha::Kotonoha* app = GetApp(appstate);
		if (app == nullptr) {
			if (event->type == SDL_EVENT_QUIT) {
				return SDL_APP_SUCCESS;
			}
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

		const SDL_AppResult status = ktrfController != nullptr
			? ktrfController->Main(&inRunning)
			: app->Main(&inRunning);
		if (status != SDL_APP_CONTINUE) {
			return status;
		}

		SDL_SetRenderTarget(global.render, nullptr);
		if (global.showFps) {
			Kotonoha_FPSRender(global.window, global.render, nullptr, nullptr, nullptr);
		}
		if (global.showTimestamp && inRunning != nullptr) {
			Kotonoha_TimestampRender(global.window, global.render, nullptr, inRunning->tm, nullptr);
		}
		if (!Kotonoha_BasicGuiRun(app, inRunning, global)) {
			return SDL_APP_SUCCESS;
		}
		SDL_RenderPresent(global.render);
		return SDL_APP_CONTINUE;
	}

	void SDL_AppQuit(void* appstate, SDL_AppResult result) {
		(void)result;

		ktrfController.reset();
		delete GetApp(appstate);
		CleanupGlobalResources();
		TTF_Quit();
		SDL_Quit();
	}
}
