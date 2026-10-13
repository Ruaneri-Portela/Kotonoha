#include <cstring>
#include <Kotonoha/Kotonoha.hpp>
#include <SDL3_ttf/SDL_ttf.h>

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

    bool PreloadFontsInto(
        ASS_Library* library,
        std::unordered_map<std::string, TTF_Font*>& fonts,
        const char* fontsDir) {

        fonts.clear();

        if (library == nullptr || fontsDir == nullptr || *fontsDir == '\0') {
            return false;
        }

        std::string baseDir;

        if (fontsDir[0] == '/') {
            // macOS, iOS, Linux
            baseDir = fontsDir;
        } else {
            const char* base = SDL_GetBasePath();

            if (base == nullptr) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to get base path: %s",
                            SDL_GetError());
                return false;
            }

            baseDir = base;

            // Remove a barra final, se existir
            if (!baseDir.empty() && baseDir.back() == '/') {
                baseDir.pop_back();
            }

            baseDir += '/';
            baseDir += fontsDir;
        }

        SDL_PathInfo info;
        if (!SDL_GetPathInfo(baseDir.c_str(), &info) ||
            info.type != SDL_PATHTYPE_DIRECTORY) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Fonts directory not found: %s",
                        baseDir.c_str());
            return false;
        }

        struct ScanContext {
            ASS_Library* lib;
            std::string dir;
            std::unordered_map<std::string, TTF_Font*>* fonts;
        };

        ScanContext ctx{library, baseDir, &fonts};
        if (!SDL_EnumerateDirectory(
                baseDir.c_str(),
                [](void* userdata, const char* dirname, const char* entry)
                    -> SDL_EnumerationResult {
                    auto* c = static_cast<ScanContext*>(userdata);

                    if (c == nullptr || c->fonts == nullptr || entry == nullptr) {
                        return SDL_ENUM_CONTINUE;
                    }

                    const std::string name(entry);

                    if (name.length() <= 4) {
                        return SDL_ENUM_CONTINUE;
                    }

                    std::string ext = name.substr(name.length() - 4);
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char ch) {
                                       return static_cast<char>(std::tolower(ch));
                                   });

                    if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") {
                        return SDL_ENUM_CONTINUE;
                    }

                    const std::string fullPath = c->dir + "/" + name;

                    size_t size = 0;
                    void* data = SDL_LoadFile(fullPath.c_str(), &size);

                    if (data == nullptr || size == 0) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "Failed to load font: %s: %s",
                                    fullPath.c_str(), SDL_GetError());
                        return SDL_ENUM_CONTINUE;
                    }

                    ass_add_font(
                        c->lib,
                        const_cast<char*>(name.c_str()),
                        static_cast<char*>(data),
                        static_cast<int>(size));

                    SDL_Log("Preloaded font into LibASS memory: %s (%zu bytes)",
                            name.c_str(), size);

                    SDL_IOStream* io = SDL_IOFromMem(data, size);

                    if (io == nullptr) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "Failed to create memory IOStream for font: %s: %s",
                                    fullPath.c_str(), SDL_GetError());
                        SDL_free(data);
                        return SDL_ENUM_CONTINUE;
                    }

                    TTF_Font* font = TTF_OpenFontIO(io, true, 48);

                    if (font == nullptr) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "Failed to open font from memory: %s: %s",
                                    fullPath.c_str(), SDL_GetError());
                        SDL_free(data);
                        return SDL_ENUM_CONTINUE;
                    }

                    const std::string fontName =
                        name.substr(0, name.length() - ext.length());

                    (*c->fonts)[fontName] = font;

                    return SDL_ENUM_CONTINUE;
                },
                &ctx)) {
            return false;
        }

        return true;
    }

	static bool InitLibAssAndTTF(
		std::unordered_map<std::string, TTF_Font*>& fonts) {

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

		if (!PreloadFontsInto(global.ass_library, fonts, "assets/fonts")) {
			return false;
		}

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

		if (!InitLibAssAndTTF(engine->fonts)) {
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

		TTF_Font* preferredFont = nullptr;

		std::unordered_map<std::string, TTF_Font*>::iterator it =
			app->fonts.find("ConcertOne-Regular");

		if (it != app->fonts.end()) {
			preferredFont = it->second;
		}

		if (global.showFps) {
			Kotonoha_FPSRender(
				global.window,
				global.render,
				nullptr,
				preferredFont,
				nullptr);
		}

		if (global.showTimestamp && inRunning != nullptr) {
			void* timestampArgs[2] = {
				inRunning->tm,
				preferredFont
			};

			Kotonoha_TimestampRender(
				global.window,
				global.render,
				nullptr,
				timestampArgs,
				nullptr);
		}

		if (inRunning != nullptr &&
			Kotonoha_IOMonitorIsLoading(&inRunning->ioMonitor)) {
			Kotonoha_LoadingRender(
				global.window, global.render, nullptr, preferredFont, nullptr);
		}
		if (!Kotonoha_BasicGuiRun(app, inRunning, global)) {
			return SDL_APP_SUCCESS;
		}
		SDL_RenderPresent(global.render);
		return SDL_APP_CONTINUE;
	}

	void SDL_AppQuit(void* appstate, SDL_AppResult result) {
		(void)result;
		delete GetApp(appstate);

		Kotonoha_UpscalingShutdown();
		CleanupGlobalResources();
		TTF_Quit();
		SDL_Quit();
	}
}
