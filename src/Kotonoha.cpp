#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <Kotonoha/Kotonoha.hpp>
#include <SDL3/SDL_video.h>
#include <string>
#include <vector>

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

		static bool ReadWholeFile(const char* path, std::string& content) {
			if (path == nullptr || *path == '\0') {
				return false;
			}

			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file.is_open()) {
				return false;
			}

			const std::streamsize size = file.tellg();
			if (size < 0) {
				return false;
			}

			file.seekg(0, std::ios::beg);
			content.resize(static_cast<size_t>(size));

			if (size > 0 && !file.read(&content[0], size)) {
				return false;
			}

			return true;
		}

	} // namespace

	Kotonoha::Kotonoha(int argc, char* argv[], SDL_AppResult* status,
		struct Kotonoha_Game& context)
		: gameContext(context) {
		Kotonoha_SetMenVoiceEnabled(&gameContext, true);
		if (status == nullptr) {
			return;
		}

		*status = SDL_APP_FAILURE;

		if (!ParseArguments(argc, argv, false)) {
			*status = SDL_APP_SUCCESS;
			return;
		}

		LoadWindowIcon("assets/icon.png");

		const char* renderMethods[] = { preferedGPU, "gpu", "software" };
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
			*status = SDL_APP_FAILURE;
			return;
		}

		gameContext.flags |= SDL_GetWindowFlags(gameContext.window);
		SDL_GetRenderVSync(gameContext.render, &gameContext.vsync);
		SDL_SetAppMetadata("A visual novel engine", "0.1", "me.hirameki.kotonoha");

		gameContext.taskLock = SDL_CreateMutex();
		if (gameContext.taskLock == nullptr) {
			SDL_LogError(0, "Failed to create mutex: %s", SDL_GetError());
			*status = SDL_APP_FAILURE;
			return;
		}

		processPoolRunning = true;
		processPool = SDL_CreateThreadRuntime(EventsThread, "Events Thread", this,
			nullptr, nullptr);
		if (processPool == nullptr) {
			SDL_LogError(0, "Failed to create worker thread: %s", SDL_GetError());
			*status = SDL_APP_FAILURE;
			return;
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

		if (!ParseArguments(argc, argv, true)) {
			*status = SDL_APP_SUCCESS;
			return;
		}
		RebuildSceneIndex();

		SDL_ShowWindow(gameContext.window);
		*status = SDL_APP_CONTINUE;
	}

	Kotonoha::~Kotonoha() {
		ClearGameplays();

		processPoolRunning = false;
		if (processPool != nullptr) {
			SDL_WaitThread(processPool, nullptr);
			processPool = nullptr;
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

	bool Kotonoha::ParseArguments(int argc, char* argv[], bool initDependent) {
		auto showHelp = []() {
			SDL_Log("Usage: Kotonoha [options]");
			SDL_Log("Options:");
			SDL_Log("  -l <file>            Load a gameplay file");
			SDL_Log("  -p <path>            Set the path for assets");
			SDL_Log("  -s <file>            Load subtitle styles file");
			SDL_Log("  -r <gpu>             Set preferred GPU");
			SDL_Log("  -g                   Show GPU render drivers list");
			SDL_Log("  -f                   Set fullscreen window mode");
			SDL_Log("  -o                   Enable OpenGL rendering context");
			SDL_Log("  -x <width> <height>  Set window size");
			SDL_Log("  -v                   Enable VSync");
			SDL_Log("  -z                   Show FPS");
			SDL_Log("  -t                   Show Timestamp");
			SDL_Log("  -u                   Show debug UI");
#ifdef KOTONOHA_DEV_CHECKPOINTS
			SDL_Log("  -k <checkpoint>      Restore a School Days DEV checkpoint");
#endif
			SDL_Log("  -h                   Show this help message");
			};

		for (int i = 1; i < argc; ++i) {
			char* arg = argv[i];
			if (arg == nullptr || *arg != '-') {
				continue;
			}

			switch (*(arg + 1)) {
			case '!':
				break;

			case 'h':
				showHelp();
				return false;

			case 'l':
				if (!initDependent) {
					break;
				}
				if (i + 1 < argc) {
					*arg = '!';
					LoadScriptFile(argv[++i]);
				}
				else {
					SDL_LogError(0, "Missing argument for -l option (gameplay file)");
                    return false;
				}
				break;

#ifdef KOTONOHA_DEV_CHECKPOINTS
			case 'k':
				if (!initDependent) {
					if (i + 1 < argc) {
						*arg = '!';
						requestedSchoolDaysCheckpoint = argv[++i];
						SDL_Log("[KTN-DEV] checkpoint requested: %s",
							requestedSchoolDaysCheckpoint.c_str());
					}
					else {
						SDL_LogError(0, "Missing argument for -k option (checkpoint)");
						return false;
					}
				}
				else if (i + 1 < argc) {
					++i;
				}
				break;
#endif

			case 'g':
				for (int j = 0; j < SDL_GetNumRenderDrivers(); ++j) {
					const char* name = SDL_GetRenderDriver(j);
					SDL_Log("Driver %d: %s", j, name ? name : "<null>");
				}
				return false;

			case 'p':
				if (i + 1 < argc) {
					*arg = '!';
					gameContext.assetsPath = argv[++i];
				}
				else {
					SDL_LogError(0, "Missing argument for -p option (assets path)");
                    return false;
				}
				break;

			case 's':
				if (!initDependent) {
					break;
				}
				if (i + 1 < argc) {
					*arg = '!';
					LoadSubtitleStylesFile(argv[++i]);
				}
				else {
					SDL_LogError(0, "Missing argument for -s option (subtitle styles file)");
                    return false;
				}
				break;

			case 'r':
				if (i + 1 < argc) {
					*arg = '!';
					preferedGPU = argv[++i];
				}
				else {
					SDL_LogError(0, "Missing argument for -r option (preferred GPU)");
                    return false;
				}
				break;

			case 'f':
				*arg = '!';
				SDL_SetWindowFullscreen(gameContext.window,
					!(gameContext.flags & SDL_WINDOW_FULLSCREEN));
				gameContext.flags = SDL_GetWindowFlags(gameContext.window);
				break;

			case 'x':
				*arg = '!';
				if (i + 2 < argc) {
					windowsWidth = SDL_atoi(argv[++i]);
					windowsHeight = SDL_atoi(argv[++i]);
					SDL_SetWindowSize(gameContext.window, windowsWidth, windowsHeight);
				}
				else {
					SDL_LogError(0, "Missing arguments for -x option (width and height)");
                    return false;
				}
				break;

			case 'v':
				if (!initDependent) {
					break;
				}
				*arg = '!';
				if (!SDL_SetRenderVSync(gameContext.render, 1)) {
					SDL_LogError(0, "Failed to enable VSync: %s", SDL_GetError());
                    return false;
				}
				break;

			case 'z':
				*arg = '!';
				gameContext.showFps = true;
				break;

			case 't':
				*arg = '!';
				gameContext.showTimestamp = true;
				break;

			case 'u':
				*arg = '!';
				Kotonoha_BasicGuiShow = true;
				break;

			default:
				SDL_LogError(0, "Unknown option: %s", arg);
				return false;
			}
		}

		return true;
	}

	void Kotonoha::LoadSubtitleStylesFile(char* path) {
		std::string content;
		if (!ReadWholeFile(path, content)) {
			SDL_LogError(0, "Erro ao carregar o arquivo de estilo: %s",
				path ? path : "<null>");
			return;
		}

		delete[] gameContext.styleStr;
		gameContext.styleStr = new char[content.size() + 1];
		std::copy(content.begin(), content.end(), gameContext.styleStr);
		gameContext.styleStr[content.size()] = '\0';
	}

	void Kotonoha::LoadWindowIcon(const char* path) {
		if (path == nullptr) {
			return;
		}

		SDL_Surface* iconSurface = Kotonoha_imageCreateSurface(path, 64, 64);
		if (iconSurface == nullptr) {
			SDL_LogError(0, "Couldn't load window icon: %s", SDL_GetError());
			return;
		}

		SDL_SetWindowIcon(gameContext.window, iconSurface);
		SDL_DestroySurface(iconSurface);
	}

	bool Kotonoha::LoadScriptFile(const char* path) {
		if (path == nullptr || *path == '\0') {
			return false;
		}

		SDL_PathInfo info;
		bool loadedAny = false;

		if (!SDL_GetPathInfo(path, &info)) {
			return false;
		}

		if (info.type == SDL_PATHTYPE_DIRECTORY) {
			struct LocalContext {
				std::vector<std::string> files;
			};

			LocalContext ctx;

			auto callback = [](void* userdata, const char* dirname,
				const char* fname) -> SDL_EnumerationResult {
					LocalContext* ctx = static_cast<LocalContext*>(userdata);
					if (ctx == nullptr || fname == nullptr) {
						return SDL_ENUM_CONTINUE;
					}

					const std::string fullPath = JoinPath(dirname, fname);

					SDL_PathInfo entryInfo;
					if (!SDL_GetPathInfo(fullPath.c_str(), &entryInfo)) {
						return SDL_ENUM_CONTINUE;
					}

					if (entryInfo.type == SDL_PATHTYPE_FILE) {
						ctx->files.emplace_back(fullPath);
					}

					return SDL_ENUM_CONTINUE;
				};

			if (!SDL_EnumerateDirectory(path, callback, &ctx)) {
				return false;
			}

			std::sort(ctx.files.begin(), ctx.files.end());

			for (const std::string& file : ctx.files) {
				try {
					Gameplay* newObject = new Gameplay(file.c_str(), &gameContext);
					gameplays.push_back(newObject);
					loadedAny = true;
				}
				catch (...) {
				}
			}

			if (loadedAny) RebuildSceneIndex();
			return loadedAny;
		}

		if (info.type == SDL_PATHTYPE_FILE) {
			try {
				Gameplay* newObject = new Gameplay(path, &gameContext);
				gameplays.push_back(newObject);
				loadedAny = true;
			}
			catch (...) {
			}
		}

		if (loadedAny) RebuildSceneIndex();
		return loadedAny;
	}

	void Kotonoha::RebuildSceneIndex() {
		sceneIndex.clear();
		for (size_t i = 0; i < gameplays.size(); ++i) {
			if (gameplays[i] == nullptr) continue;
			const SceneKey key = SceneKey::FromScriptPath(gameplays[i]->scriptPath);
			if (!key.value.empty()) sceneIndex.emplace(key, i);
		}

		const bool detected = sceneIndex.count({ "00/00-00-A00" }) &&
			sceneIndex.count({ "00/00-00-A03" }) &&
			sceneIndex.count({ "00/00-00-L00" });

		if (detected) {
			if (!schoolDaysRouting) {
				schoolDaysRouting = true;
				schoolDaysRouter.Reset();
				schoolDaysHandoff = {};
				gameContext.scene = static_cast<int>(sceneIndex.at(schoolDaysRouter.CurrentScene()));
				lastScene = static_cast<size_t>(-1);
				SDL_Log("[KTN-ROUTER] School Days full routing enabled; scene=%s choice=pending(-2) gameplay_index=%d",
					schoolDaysRouter.CurrentScene().value.c_str(), gameContext.scene);
			}

#ifdef KOTONOHA_DEV_CHECKPOINTS
			if (!requestedSchoolDaysCheckpoint.empty() && !schoolDaysCheckpointApplied) {
				const SceneKey checkpointScene =
					schoolDaysRouter.DevCheckpointScene(requestedSchoolDaysCheckpoint);

				if (checkpointScene.value.empty()) {
					SDL_Log("[KTN-DEV] unknown checkpoint: %s",
						requestedSchoolDaysCheckpoint.c_str());
					schoolDaysCheckpointApplied = true;
				}
				else {
					const auto target = sceneIndex.find(checkpointScene);
					if (target != sceneIndex.end()) {
						if (schoolDaysRouter.RestoreDevCheckpoint(requestedSchoolDaysCheckpoint)) {
							gameContext.scene = static_cast<int>(target->second);
							lastScene = static_cast<size_t>(-1);
							gameContext.next = false;
							gameContext.back = false;
							schoolDaysHandoff = {};
							schoolDaysCheckpointApplied = true;
							SDL_Log("[KTN-DEV] checkpoint applied: %s gameplay_index=%d",
								requestedSchoolDaysCheckpoint.c_str(), gameContext.scene);
							SDL_Log("[KTN-DEV] state dump\n%s",
								schoolDaysRouter.DumpState().c_str());
						}
						else {
							SDL_Log("[KTN-DEV] checkpoint restore failed: %s",
								requestedSchoolDaysCheckpoint.c_str());
							schoolDaysCheckpointApplied = true;
						}
					}
				}
			}
#endif
		}
		else {
			schoolDaysRouting = false;
			schoolDaysHandoff = {};
		}
	}

	bool Kotonoha::ContinueSchoolDaysHandoff() {
		if (!HasPendingSchoolDaysHandoff()) {
			return false;
		}

		if (schoolDaysHandoff.kind == SchoolDaysHandoffKind::Terminal) {
			SDL_Log("[KTN-HANDOFF] terminal acknowledged t%d ending=%d; title transition pending implementation",
				schoolDaysHandoff.transitionId, schoolDaysHandoff.endingId);
			return false;
		}

		const auto found = sceneIndex.find(schoolDaysHandoff.destination);
		if (found == sceneIndex.end()) {
			SDL_Log("[KTN-HANDOFF] pending destination not loaded: %s",
				schoolDaysHandoff.destination.value.c_str());
			return false;
		}

		if (gameContext.scene >= 0 &&
			static_cast<size_t>(gameContext.scene) < gameplays.size()) {
			Gameplay* current = gameplays[static_cast<size_t>(gameContext.scene)];
			if (current != nullptr && !current->firstFocus) current->Reset(true);
		}

		const SceneKey destination = schoolDaysHandoff.destination;
		const int transitionId = schoolDaysHandoff.transitionId;
		gameContext.scene = static_cast<int>(found->second);
		gameContext.next = false;
		gameContext.back = false;
		lastScene = static_cast<size_t>(-1);
		schoolDaysHandoff = {};

		SDL_Log("[KTN-HANDOFF] episode continue t%d destination=%s gameplay_index=%d",
			transitionId, destination.value.c_str(), gameContext.scene);
		return true;
	}

	SDL_AppResult Kotonoha::Event(SDL_Event* event) {
		if (event == nullptr) {
			return SDL_APP_CONTINUE;
		}

		switch (event->type) {
		case SDL_EVENT_QUIT:
			return SDL_APP_SUCCESS;

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

#ifdef KOTONOHA_DEV_CHECKPOINTS
			case SDLK_F6:
				if (schoolDaysRouting) {
					SDL_Log("[KTN-DEV] manual state dump\n%s",
						schoolDaysRouter.DumpState().c_str());
				}
				break;

			case SDLK_F5:
				if (HasPendingSchoolDaysHandoff()) {
					ContinueSchoolDaysHandoff();
				}
				else {
					SDL_Log("[KTN-HANDOFF] no pending School Days handoff");
				}
				break;
#endif

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

		Kotonoha_eventPush(&gameContext.eventQueu, *event);
		return SDL_APP_CONTINUE;
	}

	void Kotonoha::ClearGameplays() {
		for (auto* gameplay : gameplays) {
			DeleteGameplay(gameplay);
		}

		gameplays.clear();
		sceneIndex.clear();
		schoolDaysRouting = false;
		schoolDaysRouter.Reset();
		schoolDaysHandoff = {};
#ifdef KOTONOHA_DEV_CHECKPOINTS
		schoolDaysCheckpointApplied = false;
#endif
		lastScene = static_cast<size_t>(-1);
		gameContext.scene = 0;
		gameContext.next = false;
		gameContext.back = false;
	}

	SDL_AppResult Kotonoha::Main(Gameplay** out) {
		if (out != nullptr) {
			*out = nullptr;
		}

		// A callback_38 handoff is external to RouteProc. Hold the completed
		// timeline here until the outer School Days controller acknowledges it.
		// This prevents ResolveNext and its effects from running twice on later
		// frames while Save/Continue or the future Title transition is pending.
		if (HasPendingSchoolDaysHandoff()) {
			if (out != nullptr && gameContext.scene >= 0 &&
				static_cast<size_t>(gameContext.scene) < gameplays.size()) {
				*out = gameplays[static_cast<size_t>(gameContext.scene)];
			}

			Kotonoha_eventFree(&gameContext.eventQueu);
			if (SDL_GetTicks() - lastMouseTime <= 1000) SDL_ShowCursor();
			else SDL_HideCursor();
			return SDL_APP_CONTINUE;
		}

		while (!gameplays.empty() &&
			static_cast<size_t>(gameContext.scene) < gameplays.size()) {
			const size_t currentScene = static_cast<size_t>(gameContext.scene);
			Gameplay* current = gameplays[currentScene];
			if (current == nullptr) {
				if (schoolDaysRouting) {
					SDL_Log("[KTN-ROUTER] missing loaded gameplay for current scene");
					return SDL_APP_FAILURE;
				}
				lastScene = gameContext.scene++;
				continue;
			}

			if (lastScene != currentScene) {
				lastScene = currentScene;
				if (!current->firstFocus)
					current->Reset(true);
				continue;
			}

			if (gameContext.back && schoolDaysRouting) {
				SDL_Log("[KTN-ROUTER] back routing not implemented");
				gameContext.back = false;
			}
			else if (gameContext.back && gameContext.scene > 0) {
				if (!current->firstFocus)
					current->Reset(true);
				lastScene = gameContext.scene--;
				gameContext.back = false;
				continue;
			}

			const SDL_AppResult result = current->Main(&gameContext);
			if (schoolDaysRouting && current->prompt != nullptr &&
				current->prompt->Result() != -2) {
				schoolDaysRouter.AcceptChoice(current->prompt->Result());
			}
			if (schoolDaysRouting && result == SDL_APP_FAILURE) return result;
			if (result != SDL_APP_CONTINUE || gameContext.next) {
				if (schoolDaysRouting) {
					gameContext.next = false;
					if (!current->eventManager->CheckEnd(current)) {
						SDL_Log("[KTN-ROUTER] next ignored before Next scene=%s",
							schoolDaysRouter.CurrentScene().value.c_str());
						if (out != nullptr) *out = current;
						break;
					}
					const SceneKey actual = SceneKey::FromScriptPath(current->scriptPath);
					if (!(actual == schoolDaysRouter.CurrentScene())) {
						SDL_Log("[KTN-ROUTER] current scene mismatch expected=%s actual=%s",
							schoolDaysRouter.CurrentScene().value.c_str(), actual.value.c_str());
						return SDL_APP_FAILURE;
					}
					auto next = schoolDaysRouter.ResolveNext();
					std::vector<std::string> callbacks = next.callbacks;

					// RouteProc has a tiny set of routing-only dispatcher nodes
					// present in the DLL tables but absent from the physical ORS
					// script set. Resolve only those explicitly identified nodes
					// immediately; arbitrary missing assets must still fail.
					while (next.kind == SchoolDaysRouter::NextKind::Advanced &&
						sceneIndex.find(next.destination) == sceneIndex.end() &&
						schoolDaysRouter.IsCurrentSceneRoutingOnly()) {
						SDL_Log("[KTN-ROUTER] routing-only node %s (no ORS); resolving immediately",
							schoolDaysRouter.CurrentScene().value.c_str());
						next = schoolDaysRouter.ResolveNext();
						callbacks.insert(callbacks.end(), next.callbacks.begin(), next.callbacks.end());
					}
					next.callbacks = callbacks;

					if (next.kind == SchoolDaysRouter::NextKind::Unresolved) {
						if (out != nullptr) *out = current;
						break;
					}

					const bool handoffRequested = next.HasCallback("callback_38");
					if (next.kind == SchoolDaysRouter::NextKind::Terminal) {
						SDL_Log("[KTN-ROUTER] terminal transition t%d ending=%d registrations=%d",
							next.transitionId,
							next.endingId,
							static_cast<int>(schoolDaysRouter.State().endingRegistrations.size()));

						schoolDaysHandoff.kind = SchoolDaysHandoffKind::Terminal;
						schoolDaysHandoff.destination = {};
						schoolDaysHandoff.transitionId = next.transitionId;
						schoolDaysHandoff.endingId = next.endingId;
						schoolDaysHandoff.callbacks = next.callbacks;
						SDL_Log("[KTN-HANDOFF] terminal pending t%d ending=%d callback_38=%d",
							next.transitionId, next.endingId, handoffRequested ? 1 : 0);
						if (out != nullptr) *out = current;
						break;
					}

					const auto found = sceneIndex.find(next.destination);
					if (found == sceneIndex.end()) {
						SDL_Log("[KTN-ROUTER] destination not loaded: %s", next.destination.value.c_str());
						return SDL_APP_FAILURE;
					}

					if (handoffRequested) {
						schoolDaysHandoff.kind = SchoolDaysHandoffKind::Episode;
						schoolDaysHandoff.destination = next.destination;
						schoolDaysHandoff.transitionId = next.transitionId;
						schoolDaysHandoff.endingId = next.endingId;
						schoolDaysHandoff.callbacks = next.callbacks;
						SDL_Log("[KTN-HANDOFF] episode pending t%d destination=%s",
							next.transitionId, next.destination.value.c_str());
						if (out != nullptr) *out = current;
						break;
					}

					if (!current->firstFocus) current->Reset(true);
					gameContext.scene = static_cast<int>(found->second);
					continue;
				}
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

		Kotonoha_eventFree(&gameContext.eventQueu);

		if (SDL_GetTicks() - lastMouseTime <= 1000) {
			SDL_ShowCursor();
		}
		else {
			SDL_HideCursor();
		}

		return SDL_APP_CONTINUE;
	}

} // namespace Kotonoha
