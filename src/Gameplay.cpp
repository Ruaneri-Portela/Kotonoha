#include <Kotonoha/Gameplay.hpp>


namespace Kotonoha {
	namespace {
		static float MsToSeconds(Uint64 value) {
			return static_cast<float>(value) / 1000.0f;
		}
	}// namespace
	void Gameplay::UpdateCanvasSize(SDL_Window* window, SDL_Renderer* renderer) {
		if ((window == nullptr && renderer == nullptr) || drawCanvas == nullptr) {
			return;
		}

		int pixelW = 0;
		int pixelH = 0;
		if (renderer != nullptr) {
			if (!SDL_GetCurrentRenderOutputSize(renderer, &pixelW, &pixelH)) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
					"SDL_GetCurrentRenderOutputSize failed: %s", SDL_GetError());
				return;
			}
		}
		else if (!SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH)) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"SDL_GetWindowSizeInPixels failed: %s", SDL_GetError());
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

	void Gameplay::UpdateLoadingPause(struct Kotonoha_Game* gameContext) {
		if (gameContext == nullptr) {
			return;
		}

		const bool loading = Kotonoha_IOMonitorIsLoading(&ioMonitor);
		if (loading && !loadingPauseActive) {
			pausedBeforeLoading = gameContext->paused;
			loadingPauseActive = true;
		}

		if (loading) {
			gameContext->paused = true;
		}
		else if (loadingPauseActive) {
			gameContext->paused = pausedBeforeLoading;
			loadingPauseActive = false;
		}
	}

	Gameplay::Gameplay(const char* scriptPath, struct Kotonoha_Game* gameContext) {
		Kotonoha_IOMonitorInit(
			&ioMonitor, Kotonoha_IOMonitorRetryMissingAssetsEnabled());
		this->tm = Kotonoha_timeNew(true);
		this->sb = Kotonoha_TextRenderInit(this->tm, gameContext);

		this->video = new Video(this->tm, &ioMonitor);
		this->image = new Image(this->tm, &ioMonitor);
		this->audio = new Audio(
			static_cast<Sound*>(gameContext->sound), this->tm, &ioMonitor);
		this->fade = new Fade(this->tm);
		this->drawCanvas = new Canvas();
		Kotonoha_IOMonitorBindNetworkWaitFlag(
			&ioMonitor, this->drawCanvas->NetworkWaitFlag());

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

		this->drawCanvas->RegisterCanva(
			this->fade->Render, 4,
			{ 0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight) },
			this->fade);

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
		else if (firstFocus) {
			UpdateCanvasSize(gameContext->window, gameContext->render);
			if (!Kotonoha_timeIsStarted(tm)) {
				Kotonoha_timeStart(tm);
			}
			firstFocus = false;
		}
		else if (eventManager->CheckEnd(this)) {
			if (loop) {
				Reset(true);
			}
			else {
				gameContext->next = true;
				return SDL_APP_SUCCESS;
			}
		}
		 
		UpdateDeltaTime();

		if (this->putPrompt && this->prompt != nullptr) {
			drawCanvas->RegisterCanva(
				this->prompt->Render, 2,
				{ 0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight) },
				this->prompt);
			this->putPrompt = false;
		}

		const SDL_AppResult result = drawCanvas->RenderCanvas(
			gameContext->window, gameContext->render, &gameContext->eventQueu);

		UpdateLoadingPause(gameContext);
		if (gameContext->paused) {
			Pause();
		}
		else {
			Resume();
		}

		UpdateTriggers(GetDeltaTime());
		Kotonoha_eventFree(&gameContext->eventQueu);
		return result;
	}

	void Gameplay::Pause() {
		Kotonoha_timePause(this->tm);
	}

	void Gameplay::Resume() {
		Kotonoha_timeResume(this->tm);
	}

	void Gameplay::UpdateDeltaTime() {
		const Uint64 nowNs = SDL_GetTicksNS();

		if (lastFrameNs == 0) {
			lastFrameNs = nowNs;
			deltaTime = 0.0f;
			return;
		}

		const Uint64 elapsedNs = nowNs - lastFrameNs;
		lastFrameNs = nowNs;

		deltaTime =
			static_cast<float>(elapsedNs) / 1e9f;

		// Evita saltos enormes após pausas, breakpoints ou perda de foco.
		constexpr float kMaxDeltaTime = 0.25f;
		if (deltaTime > kMaxDeltaTime) {
			deltaTime = kMaxDeltaTime;
		}
	}

	float Gameplay::GetDeltaTime() const {
		return deltaTime;
	}

	void Gameplay::Reset(bool resetTime) {
		if (sb->subTexture) {
			SDL_DestroyTexture(sb->subTexture);
			sb->subTexture = nullptr;
		}
		if (resetTime) {
			firstFocus = true;
			drawCanvas->Reset();
			Kotonoha_timeReset(tm, true);
		}
		if (fade != nullptr) {
			fade->Reset();
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
		Kotonoha_IOMonitorCancel(&ioMonitor);
		delete this->eventManager;
		delete this->video;
		delete this->image;
		delete this->audio;
		Kotonoha_IOMonitorBindNetworkWaitFlag(&ioMonitor, nullptr);
		delete this->drawCanvas;
		delete this->fade;

		if (prompt) {
			delete prompt;
			prompt = nullptr;
		}

		Kotonoha_TextRenderShutdown(&(this->sb));
		Kotonoha_timeDestroy(this->tm);
	}

} // namespace Kotonoha