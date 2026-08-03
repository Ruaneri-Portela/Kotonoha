#include <Kotonoha/Gameplay.hpp>

extern bool Kotonoha_BasicGuiEditorShow;

namespace Kotonoha {

	namespace {
		constexpr Uint64 kSeekBigMs = 5000;
		constexpr Uint64 kSeekSmallMs = 500;

		static float MsToSeconds(Uint64 value) {
			return static_cast<float>(value) / 1000.0f;
		}
	} // namespace

	void Gameplay::UpdateCanvasSize(SDL_Window* window, SDL_Renderer* renderer) {
		(void)renderer;

		if (window == nullptr || drawCanvas == nullptr) {
			return;
		}

		int pixelW = 0;
		int pixelH = 0;
		if (!SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH)) {
			SDL_LogError(0, "SDL_GetWindowSizeInPixels failed: %s", SDL_GetError());
			return;
		}

		if (pixelW <= 0 || pixelH <= 0) {
			return;
		}

		windowWidth = pixelW;
		windowHeight = pixelH;

		const float renderWidth = static_cast<float>(windowWidth);
		const float renderHeight = static_cast<float>(windowHeight);
		if (renderHeight <= 0.0f || aspectRatio <= 0.0) {
			return;
		}

		SDL_FRect place{ 0.0f, 0.0f, 0.0f, 0.0f };
		const double windowAspectRatio =
			static_cast<double>(renderWidth) / static_cast<double>(renderHeight);

		if (windowAspectRatio < aspectRatio) {
			place.w = renderWidth;
			place.h = renderWidth / static_cast<float>(aspectRatio);
			place.x = 0.0f;
			place.y = (renderHeight - place.h) * 0.5f;
		}
		else {
			place.h = renderHeight;
			place.w = renderHeight * static_cast<float>(aspectRatio);
			place.y = 0.0f;
			place.x = (renderWidth - place.w) * 0.5f;
		}

		drawCanvas->UpdateCanva(nullptr, -1, place);
	}

	Gameplay::Gameplay(const char* scriptPath, struct Kotonoha_Game* gameContext) {
		this->tm = Kotonoha_timeNew(true);
		this->sb = Kotonoha_TextRenderInit(this->tm, gameContext);

		this->video = new Video(this->tm);
		this->image = new Image(this->tm);
		this->audio = new Audio(static_cast<Sound*>(gameContext->sound), this->tm);
		this->drawCanvas = new Canvas();

		this->drawCanvas->RegisterCanva(
			this->image->Render, 0,
			{ 0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight) },
			this->image);

		this->drawCanvas->RegisterCanva(
			this->video->Render, 1,
			{ 0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight) },
			this->video);

		this->drawCanvas->RegisterCanva(
			Kotonoha_TextRenderDraw, 3,
			{ 0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight) },
			this->sb);

		this->scriptPath = scriptPath ? scriptPath : "";
		this->eventManager = new Event(this->scriptPath.c_str(), this, gameContext);

#ifdef ANDROID
		playOnlyOnFocus = true;
		lastPauseStatus = gameContext->paused;
#endif
	}

	SDL_AppResult Gameplay::Main(struct Kotonoha_Game* gameContext) {
		if (gameContext == nullptr || eventManager == nullptr || drawCanvas == nullptr) {
			return SDL_APP_FAILURE;
		}

		if (eventManager->CheckEnd(this)) {
			if (loop) {
				Reset(true);
			}
			else {
				return SDL_APP_SUCCESS;
			}
		}


		void* persistent = nullptr;

		do {
			SDL_Event event = Kotonoha_eventRead(&gameContext->eventQueu, &persistent);
			switch (event.type) {
			case SDL_EVENT_KEY_DOWN:
				if (!Kotonoha_BasicGuiEditorShow) {
					switch (event.key.key) {
					case SDLK_P:
						gameContext->paused = !gameContext->paused;
						break;

					case SDLK_I: {
						const float timeInSeconds = GetTime();
						SDL_Log("Time: %.2f, is paused %d\n",
							timeInSeconds,
							Kotonoha_timeIsPaused(this->tm));
						break;
					}

					case SDLK_S:
						Kotonoha_eventFree(&gameContext->eventQueu);
						return SDL_APP_SUCCESS;

					case SDLK_N: {
						Uint64 currentTime = Kotonoha_timeGet(this->tm);
						if (currentTime < eventManager->lastTime - kSeekSmallMs) {
							SeekForward(kSeekBigMs);
							break;
						}
						gameContext->next = true;
						break;
					}
					case SDLK_B: {
						Uint64 currentTime = Kotonoha_timeGet(this->tm);
						if (currentTime > kSeekSmallMs) {
							SeekBackward(kSeekBigMs);
							break;
						}
						gameContext->back = true;
						break;
					}
					case SDLK_Q:
						SeekForward(kSeekSmallMs);
						break;

					case SDLK_E:
						SeekBackward(kSeekSmallMs);
						break;

					case SDLK_R:
						Reset(true);
						break;

					default:
						break;
					}
					break;
				}
			case SDL_EVENT_WINDOW_RESIZED:
				UpdateCanvasSize(gameContext->window, gameContext->render);
				break;

#ifdef Kotonoha_MobileSetup
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				if (playOnlyOnFocus) {
					gameContext->paused = lastPauseStatus;
					if (!gameContext->paused) {
						Resume();
					}
				}
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				if (playOnlyOnFocus) {
					lastPauseStatus = gameContext->paused;
					gameContext->paused = true;
					Pause();
				}
				break;
#endif

			default:
				break;
			}
		} while (persistent != nullptr);

		if (this->putPrompt && this->prompt != nullptr) {
			drawCanvas->RegisterCanva(
				this->prompt->Render, 2,
				{ 0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight) },
				this->prompt);
			this->putPrompt = false;
		}

		if (!Kotonoha_timeIsStarted(tm)) {
			Kotonoha_timeStart(tm);
		}

		if (gameContext->paused) {
			Pause();
		}
		else {
			Resume();
		}

		if (firstFocus) {
			UpdateCanvasSize(gameContext->window, gameContext->render);
			firstFocus = false;
		}

		const SDL_AppResult result = drawCanvas->RenderCanvas(
			gameContext->window, gameContext->render, &gameContext->eventQueu);

		Kotonoha_eventFree(&gameContext->eventQueu);
		return result;
	}

	void Gameplay::Pause() {
		Kotonoha_timePause(this->tm);
	}

	void Gameplay::Resume() {
		Kotonoha_timeResume(this->tm);
	}

	void Gameplay::Reset(bool resetTime) {
		if (resetTime) {
			firstFocus = true;
			drawCanvas->Reset();
			Kotonoha_timeReset(tm, true);
		}
		eventManager->Reset(this);
	}

	void Gameplay::SeekForward(Uint64 ms) {
		Kotonoha_timeSeekForward(this->tm, ms);
	}

	void Gameplay::SeekBackward(Uint64 ms) {
		Kotonoha_timeSeekBackward(this->tm, ms);
		Reset(false);
	}

	float Gameplay::GetLastTime() {
		return MsToSeconds(static_cast<Uint64>(this->eventManager->lastTime));
	}

	float Gameplay::GetTime() {
		return MsToSeconds(Kotonoha_timeGet(this->tm));
	}

	void Gameplay::SetTime(float time) {
		const float currentTime = GetTime();
		Kotonoha_timeSet(this->tm, static_cast<Uint64>(time * 1000.0f));
		if (time < currentTime) {
			Reset(false);
		}
	}

	Gameplay::~Gameplay() {
		delete this->eventManager;
		delete this->drawCanvas;
		delete this->video;
		delete this->image;
		delete this->audio;

		if (prompt) {
			delete prompt;
			prompt = nullptr;
		}

		Kotonoha_TextRenderShutdown(&(this->sb));
		Kotonoha_timeDestroy(this->tm);
	}

} // namespace Kotonoha