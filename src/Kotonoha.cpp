#include <algorithm>
#include <cstddef>
#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <string>
#include <vector>
#include <cstring>

extern "C"{
	#include <Kotonoha/speciais/Upscaling.h>
}

extern bool Kotonoha_BasicGuiShow;
extern bool Kotonoha_BasicGuiEditorShow;

namespace Kotonoha {

	namespace {

		static std::string JoinPath(const char* dirname, const char* fname) {
			if (dirname == nullptr || *dirname == '\0') {
				return fname ? std::string(fname) : std::string();
			}

			if (fname == nullptr || *fname == '\0') {
				return std::string(dirname);
			}

			std::string result(dirname);
			const char last = result.back();
			if (last != '/' && last != '\\') {
#ifdef _WIN32
				result += '\\';
#else
				result += '/';
#endif
			}

			result += fname;
			return result;
		}
	} // namespace

	Kotonoha::Kotonoha(int argc, char* argv[], SDL_AppResult* status,
		struct Kotonoha_Game& context)
		: gameContext(context) {
		if (status == nullptr) {
			return;
		}

		Kotonoha_IOMonitorSetRetryMissingAssets(true);

		if (!ParseArguments(argc, argv)) {
			*status = SDL_APP_SUCCESS;
			return;
		}

		*status = SDL_APP_CONTINUE;
	}

	void Kotonoha::ConfigureWindow() {
		if (fullscreen) {
			SDL_SetWindowFullscreen(gameContext.window, true);
			gameContext.flags = SDL_GetWindowFlags(gameContext.window);
		}
		if (gameContext.videoWidth != 1280 || gameContext.videoHeight != 720) {
			SDL_SetWindowSize(gameContext.window, gameContext.videoWidth, gameContext.videoHeight);
		}
	}

	SDL_AppResult Kotonoha::Initialize() {
		Kotonoha_IOMonitorSetRetryMissingAssets(!ignoreMissingAssets);

#ifndef KOTONOHA_MOBILE
		LoadWindowIcon("assets/icon.png");
#endif
		const char* renderMethods[] = { preferredGPU, "gpu", "software" };
		const size_t numMethods = sizeof(renderMethods) / sizeof(renderMethods[0]);

		for (size_t i = 0; i < numMethods; ++i) {
			const char* method = renderMethods[i];
			if (method == nullptr || *method == '\0') {
				continue;
			}

			gameContext.render = SDL_CreateRenderer(gameContext.window, method);
			if (gameContext.render != nullptr) {
				SDL_Log("Renderer created with method '%s'", method);
				break;
			}

			SDL_LogError(0,
				"Failed to create renderer with method '%s': %s",
				method,
				SDL_GetError());
		}

		if (gameContext.render == nullptr) {
			SDL_LogError(0, "Couldn't create renderer with any method, giving up: %s",
				SDL_GetError());
			return SDL_APP_FAILURE;
		}

		gameContext.flags |= SDL_GetWindowFlags(gameContext.window);
		SDL_GetRenderVSync(gameContext.render, &gameContext.vsync);
		SDL_SetAppMetadata("A visual novel engine", "0.1", "me.hirameki.kotonoha");

		if (enableVsync && !SDL_SetRenderVSync(gameContext.render, 1)) {
			SDL_LogError(0, "Failed to enable VSync: %s", SDL_GetError());
			return SDL_APP_FAILURE;
		}
		gameContext.showFps = showFps;
		gameContext.showTimestamp = showTimestamp;
		if (showDebugUi) {
			Kotonoha_BasicGuiShow = true;
		}

		gameContext.taskLock = SDL_CreateMutex();
		if (gameContext.taskLock == nullptr) {
			SDL_LogError(0, "Failed to create mutex: %s", SDL_GetError());
			return SDL_APP_FAILURE;
		}

		processPoolRunning = true;
		processPool = SDL_CreateThreadRuntime(EventsThread, "Events Thread", this,
			nullptr, nullptr);
		if (processPool == nullptr) {
			SDL_LogError(0, "Failed to create worker thread: %s", SDL_GetError());
			return SDL_APP_FAILURE;
		}

		gameContext.processPoolTasks = &processPoolTasks;

		Sound* sound = static_cast<Sound*>(gameContext.sound);
		if (sound != nullptr) {
			BGM = sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "BGM", nullptr);
			Se = sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "Se", nullptr);
			Voice = sound->CreateChannel(SDL_AUDIO_F32, 2, 48000, false, "Voice", nullptr);
		}

		SDL_Surface* cursorImg =
			Kotonoha_imageCreateSurface("assets/test_cursor.png", 45, 32);
		if (cursorImg != nullptr) {
			cursor = SDL_CreateColorCursor(cursorImg, 0, 0);
			if (cursor != nullptr) {
				SDL_SetCursor(cursor);
			}
			SDL_DestroySurface(cursorImg);
		}

        LoadSubtitleStylesFile("assets/styles.skot");
        
		for (const auto& file : deferredFiles) {
			if (file.isSubtitleStyles) {
				LoadSubtitleStylesFile(file.path.c_str());
			}
			else {
				LoadScriptFile(file.path.c_str());
			}
		}

		return SDL_APP_CONTINUE;
	}

	Kotonoha::~Kotonoha() {
		for (auto* gameplay : gameplays) {
			if (gameplay != nullptr) {
				Kotonoha_IOMonitorCancel(&gameplay->ioMonitor);
			}
		}

		processPoolRunning = false;

		if (processPool != nullptr) {
			SDL_WaitThread(processPool, nullptr);
			processPool = nullptr;
		}

		ClearGameplays();

		for (auto& task : processPoolTasks) {
			SDL_free(std::get<1>(task));
		}
		processPoolTasks.clear();

		if (gameContext.styleStr != nullptr) {
			delete[] gameContext.styleStr;
			gameContext.styleStr = nullptr;
		}

		if (cursor != nullptr) {
			SDL_DestroyCursor(cursor);
			cursor = nullptr;
		}

		if (gameContext.taskLock != nullptr) {
			SDL_DestroyMutex(gameContext.taskLock);
			gameContext.taskLock = nullptr;
		}

		if (gameContext.render != nullptr) {
			SDL_DestroyRenderer(gameContext.render);
			gameContext.render = nullptr;
		}

		if (gameContext.window != nullptr) {
			SDL_DestroyWindow(gameContext.window);
			gameContext.window = nullptr;
		}

		for (std::unordered_map<std::string, TTF_Font*>::iterator it = fonts.begin();
			it != fonts.end();
			++it) {
			if (it->second != nullptr) {
				TTF_CloseFont(it->second);
			}
		}

		fonts.clear();
}

	int Kotonoha::EventsThread(void* data) {
		Kotonoha* process = static_cast<Kotonoha*>(data);
		if (process == nullptr || process->gameContext.taskLock == nullptr) {
			return -1;
		}

		auto& tasks = process->processPoolTasks;
		Uint64 lastTime = SDL_GetTicks();

		while (process->processPoolRunning) {
			const Uint64 now = SDL_GetTicks();
			const Uint64 elapsed = now - lastTime;

			if (elapsed < 50) {
				SDL_Delay(static_cast<Uint32>(50 - elapsed));
			}
			if (!process->processPoolRunning) {
				break;
			}

			SDL_LockMutex(process->gameContext.taskLock);

			for (auto it = tasks.begin(); it != tasks.end();) {
				const int result = std::get<0>(*it)(std::get<1>(*it));
				if (result == -1) {
					SDL_free(std::get<1>(*it));
					it = tasks.erase(it);
				}
				else {
					++it;
				}
			}

			SDL_UnlockMutex(process->gameContext.taskLock);
			lastTime = SDL_GetTicks();
		}

		return 0;
	}

	void Kotonoha::DeleteGameplay(Gameplay* gameplay) {
		if (gameplay == nullptr) {
			return;
		}

		if (gameContext.taskLock != nullptr) {
			SDL_LockMutex(gameContext.taskLock);

			for (auto it = processPoolTasks.begin(); it != processPoolTasks.end();) {
				void* taskData = std::get<1>(*it);
				bool removeTask = false;

				if (taskData != nullptr) {
					void** parms = static_cast<void**>(taskData);

					if (parms[0] == gameplay) {
						removeTask = true;
					}
				}

				if (removeTask) {
					SDL_free(taskData);
					it = processPoolTasks.erase(it);
				}
				else {
					++it;
				}
			}

			SDL_UnlockMutex(gameContext.taskLock);
		}

		delete gameplay;
	}

	bool Kotonoha::ParseArguments(int argc, char* argv[]) {
		auto showHelp = []() {
			SDL_Log("Usage: Kotonoha [options]");
			SDL_Log("Options:");
			SDL_Log("  -l <file>            Load a gameplay file");
			SDL_Log("  -p <path>            Set the path for assets");
			SDL_Log("  -i                   Skip assets that do not exist");
			SDL_Log("  -s <file>            Load subtitle styles file");
			SDL_Log("  -r <gpu>             Set preferred GPU");
			SDL_Log("  -g                   Show GPU render drivers list");
			SDL_Log("  -a                   Show Audio render drivers list");
			SDL_Log("  -f                   Set fullscreen window mode");
			SDL_Log("  -x <width> <height>  Set window size");
			SDL_Log("  -q <factor>         Set upscale factor; 1.0 disables MetalFX");
			SDL_Log("  -v                   Enable VSync");
			SDL_Log("  -z                   Show FPS");
			SDL_Log("  -t                   Show Timestamp");
			SDL_Log("  -u                   Show debug UI");
			SDL_Log("  -h                   Show this help message");
			};

		bool cancellOpen = false;

		for (int i = 1; i < argc; ++i) {
			char* arg = argv[i];
			if (arg == nullptr || *arg != '-') {
				continue;
			}

			switch (*(arg + 1)) {
			case 'g':
				SDL_Log("GPU render drivers:");
				for (int j = 0; j < SDL_GetNumRenderDrivers(); ++j) {
					const char* name = SDL_GetRenderDriver(j);
					SDL_Log("Driver %d: %s", j, name ? name : "<null>");
				}
				cancellOpen = true;
				break;

			case 'a':
				SDL_Log("Audio render drivers:");
				for (int j = 0; j < SDL_GetNumAudioDrivers(); ++j) {
					const char* name = SDL_GetAudioDriver(j);
					SDL_Log("Driver %d: %s", j, name ? name : "<null>");
				}
				cancellOpen = true;
				break;

			case 'h':
				showHelp();
				cancellOpen = true;
				break;
			
			case 'l':
				if (i + 1 < argc && argv[i + 1] != nullptr) {
					deferredFiles.push_back({ argv[++i], false });
				}
				else {
					SDL_LogError(0, "Missing argument for -l option (gameplay file)");
					return false;
				}
				break;
			case 'p':
				if (i + 1 < argc && argv[i + 1] != nullptr) {
					std::string assetsPath = JoinPath(argv[++i], " ");

					if (!assetsPath.empty()) {
						assetsPath.pop_back();
					}

					gameContext.assetsPath = SDL_strdup(assetsPath.c_str());
				}
				else {
					SDL_LogError(0, "Missing argument for -p option (assets path)");
					return false;
				}
				break;

			case 's':
				if (i + 1 < argc && argv[i + 1] != nullptr) {
					deferredFiles.push_back({ argv[++i], true });
				}
				else {
					SDL_LogError(0, "Missing argument for -s option (subtitle styles file)");
					return false;
				}
				break;

			case 'r':
				if (i + 1 < argc && argv[i + 1] != nullptr) {
					preferredGPU = argv[++i];
				}
				else {
					SDL_LogError(0, "Missing argument for -r option (preferred GPU)");
					return false;
				}
				break;

			case 'x':
				if (i + 2 < argc && argv[i + 1] != nullptr &&
					argv[i + 2] != nullptr) {
					gameContext.videoWidth = SDL_atoi(argv[++i]);
					gameContext.videoHeight = SDL_atoi(argv[++i]);
				}
				else {
					SDL_LogError(0, "Missing arguments for -x option (width and height)");
					return false;
				}
				break;
			case 'q':
				if (i + 1 < argc && argv[i + 1] != nullptr) {
					const char *factorText = argv[++i];

					char *end = nullptr;
					errno = 0;

					const float factor = std::strtof(factorText, &end);

					if (end == factorText ||
						*end != '\0' ||
						errno == ERANGE ||
						!std::isfinite(factor) ||
						factor <= 0.0f) {
						SDL_LogError(
							0,
							"Invalid value for -q upscale factor: %s",
							factorText
						);

						return false;
					}

					Kotonoha_UpscalingSetFactor(factor);

					SDL_Log(
						"Upscaling factor set to %.3f%s",
						factor,
						factor > 1.0f
							? " (Upscale requestedc)"
							: " (native resolution)"
					);
				}
				else {
					SDL_LogError(
						0,
						"Missing argument for -q option (upscale factor)"
					);

					return false;
				}
				break;
			case 'f':
				fullscreen = !fullscreen;
				break;
			
			case 'i':
				ignoreMissingAssets = true;
				break;

			case 'v':
				enableVsync = true;
				break;

			case 'z':
				showFps = true;
				break;

			case 't':
				showTimestamp = true;
				break;

			case 'u':
				showDebugUi = true;
				break;

			default:
				SDL_LogError(0, "Unknown option: %s", arg);
				return false;
			}
		}

		if(cancellOpen) {
			return false;
		}
		return true;
	}

	void Kotonoha::LoadSubtitleStylesFile(const char* path) {
		size_t size = 0;
		void* data = SDL_LoadFile(path, &size);

		if (data != nullptr && size > 0) {
			delete[] gameContext.styleStr;

			gameContext.styleStr = new char[size + 1];
			std::memcpy(gameContext.styleStr, data, size);
			gameContext.styleStr[size] = '\0';
		}

		SDL_free(data);
	}

	void Kotonoha::LoadWindowIcon(const char* path) {
		if (path == nullptr) {
			return;
		}

		SDL_Surface* iconSurface = Kotonoha_imageCreateSurface(path, 64, 64);
		if (iconSurface == nullptr) {
			if (Kotonoha_IOMonitorRetryMissingAssetsEnabled()) {
				SDL_LogError(0, "Couldn't load window icon: %s", SDL_GetError());
			}
			return;
		}

		SDL_SetWindowIcon(gameContext.window, iconSurface);
		SDL_DestroySurface(iconSurface);
	}

    bool Kotonoha::LoadScriptFile(const char* path) {
        if (path == nullptr || *path == '\0') {
            return false;
        }

        std::string fullPath;

        const bool isAbsolute =
            path[0] == '/' ||
            (path[0] != '\0' && path[1] == ':' &&
             (path[2] == '\\' || path[2] == '/')) ||
            (path[0] == '\\' && path[1] == '\\');

        if (isAbsolute) {
            fullPath = path;
        } else {
            const char* base = SDL_GetBasePath();

            if (base == nullptr) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to get base path: %s",
                            SDL_GetError());
                return false;
            }

            fullPath = base;

            if (!fullPath.empty() && fullPath.back() == '/') {
                fullPath.pop_back();
            }

            fullPath += '/';
            fullPath += path;
        }

        SDL_PathInfo info;
        bool loadedAny = false;

        if (!SDL_GetPathInfo(fullPath.c_str(), &info)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Script path not found: %s",
                        fullPath.c_str());
            return false;
        }

        TTF_Font* preferredFont = nullptr;

        std::unordered_map<std::string, TTF_Font*>::iterator it =
            fonts.find("ConcertOne-Regular");

        if (it != fonts.end()) {
            preferredFont = it->second;
        }

        if (info.type == SDL_PATHTYPE_DIRECTORY) {
            std::vector<std::string> files;

            if (!SDL_EnumerateDirectory(
                    fullPath.c_str(),
                    [](void* userdata, const char* dirname, const char* fname)
                        -> SDL_EnumerationResult {
                        auto* files =
                            static_cast<std::vector<std::string>*>(userdata);

                        if (files == nullptr || fname == nullptr) {
                            return SDL_ENUM_CONTINUE;
                        }

                        const std::string entryPath = JoinPath(dirname, fname);

                        SDL_PathInfo entryInfo;
                        if (!SDL_GetPathInfo(entryPath.c_str(), &entryInfo)) {
                            return SDL_ENUM_CONTINUE;
                        }

                        if (entryInfo.type == SDL_PATHTYPE_FILE) {
                            files->emplace_back(entryPath);
                        }

                        return SDL_ENUM_CONTINUE;
                    },
                    &files)) {
                return false;
            }

            std::sort(files.begin(), files.end());

            for (const std::string& file : files) {
                try {
                    Gameplay* newObject =
                        new Gameplay(file.c_str(), &gameContext, preferredFont);

                    gameplays.push_back(newObject);
                    loadedAny = true;
                } catch (...) {
                }
            }

            return loadedAny;
        }

    if (info.type == SDL_PATHTYPE_FILE) {
        try {
            Gameplay* newObject =
                new Gameplay(fullPath.c_str(), &gameContext, preferredFont);

            gameplays.push_back(newObject);
            loadedAny = true;
        } catch (...) {
        }
    }

    return loadedAny;
}

	SDL_AppResult Kotonoha::Event(SDL_Event* event) {
		if (event == nullptr) {
			return SDL_APP_CONTINUE;
		}

		switch (event->type) {
		case SDL_EVENT_KEY_DOWN:
			switch (event->key.key) {
			case SDLK_AC_BACK:
				return SDL_APP_SUCCESS;

			case SDLK_F11:
				SDL_SetWindowFullscreen(gameContext.window,
					!(gameContext.flags & SDL_WINDOW_FULLSCREEN));
				gameContext.flags = SDL_GetWindowFlags(gameContext.window);
				break;

			case SDLK_F10:
				gameContext.showFps = !gameContext.showFps;
				break;

			case SDLK_F9:
				if (!SDL_GetRenderVSync(gameContext.render, &gameContext.vsync)) {
					SDL_LogError(0, "Failed to get VSync state: %s", SDL_GetError());
					break;
				}

				if (!SDL_SetRenderVSync(gameContext.render,
					gameContext.vsync ? 0 : 1)) {
					SDL_LogError(0, "Failed to toggle VSync: %s", SDL_GetError());
				}

				SDL_GetRenderVSync(gameContext.render, &gameContext.vsync);
				break;

			case SDLK_F8:
				gameContext.showTimestamp = !gameContext.showTimestamp;
				break;

			case SDLK_F7:
				Kotonoha_BasicGuiShow = !Kotonoha_BasicGuiShow;
				break;

			default:
				break;
			}
			break;

		case SDL_EVENT_MOUSE_MOTION:
			lastMouseTime = SDL_GetTicks();
			break;

		default:
			break;
		}

		if(gameContext.scene >= 0 && static_cast<size_t>(gameContext.scene) < gameplays.size()) {
			Gameplay* current = gameplays[static_cast<size_t>(gameContext.scene)];
			if (current != nullptr) {
				current->Events(event, &gameContext);
			}
		}
		Kotonoha_eventPush(&gameContext.eventQueu, *event);
		return SDL_APP_CONTINUE;
	}

	void Kotonoha::ClearGameplays() {
		for (auto* gameplay : gameplays) {
			DeleteGameplay(gameplay);
		}

		gameplays.clear();
		gameContext.scene = 0;
		gameContext.next = false;
		gameContext.back = false;
		lastScene = static_cast<size_t>(-1);
		sceneTransitionPrepared = false;
		sceneTransitionActive = false;
		lastSceneFrameCaptureTime = 0;
		if (sceneTransitionFrame != nullptr) {
			SDL_DestroyTexture(sceneTransitionFrame);
			sceneTransitionFrame = nullptr;
		}
	}

	bool Kotonoha::CaptureSceneTransitionFrame(bool activateTransition) {
		if (gameContext.render == nullptr) {
			return false;
		}
		if (SDL_GetRenderTarget(gameContext.render) != nullptr) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Scene transition capture skipped: render target is not the backbuffer");
			return false;
		}

		SDL_Surface* surface = SDL_RenderReadPixels(gameContext.render, nullptr);
		if (surface == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Scene transition frame capture failed: %s", SDL_GetError());
			return false;
		}

		SDL_Texture* nextFrame = SDL_CreateTextureFromSurface(gameContext.render, surface);
		if (nextFrame == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Scene transition texture creation failed: %s", SDL_GetError());
			SDL_DestroySurface(surface);
			return false;
		}
		SDL_DestroySurface(surface);

		if (!SDL_SetTextureBlendMode(nextFrame, SDL_BLENDMODE_NONE)) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Scene transition texture blend setup failed: %s", SDL_GetError());
			SDL_DestroyTexture(nextFrame);
			return false;
		}

		if (sceneTransitionFrame != nullptr) {
			SDL_DestroyTexture(sceneTransitionFrame);
		}
		sceneTransitionFrame = nextFrame;
		if (activateTransition) {
			sceneTransitionPrepared = true;
			sceneTransitionActive = true;
		}
		return true;
	}

	void Kotonoha::CacheSceneTransitionFrameIfNeeded(Gameplay* current) {
		if (sceneTransitionActive || sceneTransitionPrepared ||
			current == nullptr || current->eventManager == nullptr ||
			current->drawCanvas == nullptr ||
			!current->drawCanvas->DrewAtOrBelow(1)) {
			return;
		}

		const float remaining = current->GetLastTime() - current->GetTime();
		if (remaining < -0.1f || remaining > 1.0f) {
			return;
		}

		const Uint64 now = SDL_GetTicks();
		if (now - lastSceneFrameCaptureTime < 100) {
			return;
		}
		if (CaptureSceneTransitionFrame(false)) {
			lastSceneFrameCaptureTime = now;
		}
	}

	void Kotonoha::PresentSceneTransitionFrame(Gameplay* incoming) {
		if (!sceneTransitionActive || sceneTransitionFrame == nullptr) {
			return;
		}

		if (incoming == nullptr &&
			(gameContext.scene < 0 ||
				static_cast<size_t>(gameContext.scene) >= gameplays.size())) {
			SDL_DestroyTexture(sceneTransitionFrame);
			sceneTransitionFrame = nullptr;
			sceneTransitionPrepared = false;
			sceneTransitionActive = false;
			lastSceneFrameCaptureTime = 0;
			return;
		}

		if (incoming != nullptr && incoming->drawCanvas != nullptr &&
			incoming->drawCanvas->DrewAtOrBelow(1)) {
			SDL_DestroyTexture(sceneTransitionFrame);
			sceneTransitionFrame = nullptr;
			sceneTransitionPrepared = false;
			sceneTransitionActive = false;
			lastSceneFrameCaptureTime = 0;
			return;
		}

		if (gameContext.render == nullptr) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Cannot present retained scene frame without a renderer");
			return;
		}
		if (SDL_GetRenderTarget(gameContext.render) != nullptr &&
			!SDL_SetRenderTarget(gameContext.render, nullptr)) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Cannot restore backbuffer for retained scene frame: %s",
				SDL_GetError());
			return;
		}
		if (!SDL_RenderTexture(gameContext.render, sceneTransitionFrame, nullptr, nullptr)) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Retained scene frame presentation failed: %s", SDL_GetError());
		}
	}

	SDL_AppResult Kotonoha::Main(Gameplay** out) {
		SDL_AppResult status = SDL_APP_CONTINUE;
		if (out != nullptr) {
			*out = nullptr;
		}

		while (!gameplays.empty() &&
			static_cast<size_t>(gameContext.scene) < gameplays.size()) {
			const size_t currentScene = static_cast<size_t>(gameContext.scene);
			Gameplay* current = gameplays[currentScene];
			if (current == nullptr) {
				lastScene = gameContext.scene++;
				continue;
			}

			if (lastScene != currentScene) {
				if (lastScene != static_cast<size_t>(-1) &&
					sceneTransitionFrame != nullptr) {
					sceneTransitionActive = true;
					sceneTransitionPrepared = false;
				}
				lastScene = currentScene;
				if (!current->firstFocus)
					current->Reset(true);
					#ifdef KOTONOHA_HAS_METALFX
					Kotonoha_MetalFXClearScalerCache();
					#endif
				continue;
			}

			if (gameContext.back && gameContext.scene > 0) {
				if (!current->firstFocus)
					current->Reset(true);
				lastScene = gameContext.scene--;
				gameContext.back = false;
				continue;
			}

			const SDL_AppResult result = current->Main(&gameContext);
			if (result == SDL_APP_FAILURE) {
				status = result;
				break;
			}
			if ((gameContext.next || gameContext.back) &&
				result == SDL_APP_CONTINUE && !sceneTransitionPrepared) {
				CaptureSceneTransitionFrame();
			}
			if (result != SDL_APP_CONTINUE || gameContext.next) {
				if (!current->firstFocus)
					current->Reset(true);
				gameContext.scene++;
				gameContext.next = false;

				if (static_cast<size_t>(gameContext.scene) < gameplays.size()) {
					continue;
				}

				lastScene = static_cast<size_t>(gameContext.scene);
				break;
			}

			if (out != nullptr) {
				*out = current;
			}
			break;
		}

		const bool sceneChangePending =
			gameContext.next || (gameContext.back && gameContext.scene > 0);
		if (sceneTransitionPrepared && !sceneTransitionActive &&
			!sceneChangePending) {
			if (sceneTransitionFrame != nullptr) {
				SDL_DestroyTexture(sceneTransitionFrame);
				sceneTransitionFrame = nullptr;
			}
			sceneTransitionPrepared = false;
		}

		if (Kotonoha_BasicGuiShow ||
			SDL_GetTicks() - lastMouseTime <= 1000) {
			SDL_ShowCursor();
		}
		else if (!Kotonoha_BasicGuiShow &&
			!SDL_GetWindowMouseGrab(gameContext.window)) {
			SDL_HideCursor();
		}

		return status;
	}

} // namespace Kotonoha
