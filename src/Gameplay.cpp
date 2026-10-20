#include <Kotonoha/Gameplay.hpp>

#include <Kotonoha/specials/MetalFX.h>

#include <SDL3_ttf/SDL_ttf.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace Kotonoha {
namespace {
static float MsToSeconds(Uint64 value) {
  return static_cast<float>(value) / 1000.0f;
}
} // namespace
void Gameplay::UpdateCanvasSize(SDL_Window *window, SDL_Renderer *renderer) {
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
  } else if (!SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH)) {
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

  SDL_FRect place{0.0f, 0.0f, 0.0f, 0.0f};
  const double windowAspectRatio =
      static_cast<double>(renderWidth) / static_cast<double>(renderHeight);

  if (windowAspectRatio < aspectRatio) {
    place.w = renderWidth;
    place.h = renderWidth / static_cast<float>(aspectRatio);
    place.x = 0.0f;
    place.y = (renderHeight - place.h) * 0.5f;
  } else {
    place.h = renderHeight;
    place.w = renderHeight * static_cast<float>(aspectRatio);
    place.y = 0.0f;
    place.x = (renderWidth - place.w) * 0.5f;
  }

  canvasPlace = place;
  drawCanvas->UpdateCanva(nullptr, -1, place);
  if (prompt != nullptr) {
    prompt->SetViewport(place);
  }
}

void Gameplay::UpdateLoadingPause(struct Kotonoha_Game *gameContext) {
  if (gameContext == nullptr) {
    return;
  }

  const bool loading = KotonohaDemuxerIsLoading(demuxer);
  const Uint64 now = SDL_GetTicks();
  constexpr Uint64 kLoadingDebounceMs = 300;
  constexpr Uint64 kMinimumLoadingPauseMs = 1000;

  if (loading) {
    if (!loadingCandidateActive) {
      loadingCandidateActive = true;
      loadingCandidateStartedAt = now;
    }
    if (now - loadingCandidateStartedAt < kLoadingDebounceMs) {
      return;
    }

    if (!loadingPauseActive) {
      pausedBeforeLoading = gameContext->config.paused;
      loadingPauseActive = true;
      loadingPauseStartedAt = now;
    }
    loadingOverlayVisibleUntil = now + kMinimumLoadingPauseMs;
    gameContext->config.paused = true;
  } else {
    loadingCandidateActive = false;
    loadingCandidateStartedAt = 0;
    if (loadingPauseActive && now >= loadingOverlayVisibleUntil &&
        now - loadingPauseStartedAt >= kMinimumLoadingPauseMs) {
      gameContext->config.paused = pausedBeforeLoading;
      loadingPauseActive = false;
      loadingPauseStartedAt = 0;
    }
  }
}

Gameplay::Gameplay(const char *scriptPath, Kotonoha_Game *gameContext,
                   TTF_Font *font) {
  this->demuxer = KotonohaDemuxerCreate(!gameContext->config.skipFiles);
  this->tm = KotonohaTimeNew(true);
  this->sb = KotonohaTextRenderInit(this->tm, gameContext);
  this->video = new Video(this->tm, this->demuxer,
                          gameContext->config.videoCacheSeconds);
  this->image = new Image(this->tm, gameContext->config.skipFiles);
  this->audio = new Audio(static_cast<Sound *>(gameContext->sound), this->tm,
                          this->demuxer);
  this->fade = new Fade(this->tm);
  this->drawCanvas = new Canvas();

  this->drawCanvas->RegisterCanva(
      this->image->Render, 0,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->image);

  this->drawCanvas->RegisterCanva(
      this->video->Render, 1,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->video);

  this->drawCanvas->RegisterCanva(
      KotonohaTextRenderDraw, 3,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->sb);

  this->drawCanvas->RegisterCanva(
      this->fade->Render, 4,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->fade);

  this->scriptPath = scriptPath ? scriptPath : "";
  this->eventManager =
      new Event(this->scriptPath.c_str(), this, gameContext, font);
#if defined(__ANDROID__) || defined(ANDROID)
  playOnlyOnFocus = true;
  lastPauseStatus = gameContext->config.paused;
#endif
}

SDL_AppResult Gameplay::Main(Kotonoha_Game *gameContext) {
  if (gameContext == nullptr || eventManager == nullptr ||
      drawCanvas == nullptr) {
    return SDL_APP_FAILURE;
  }
  if (firstFocus) {
    UpdateCanvasSize(gameContext->window, gameContext->render);
    if (prefetchPendingPromotion) {
      KotonohaTimeStart(tm);
      prefetchPendingPromotion = false;
    } else if (!KotonohaTimeIsStarted(tm)) {
      KotonohaTimeStart(tm);
    }
    firstFocus = false;
    KotonohaDemuxerStart(this->demuxer);
  } else if (eventManager->CheckEnd(this)) {
    if (loop) {
      Reset(true);
    } else {
      gameContext->config.next = true;
      return SDL_APP_SUCCESS;
    }
  }

  eventManager->ActivateSubtitles();
  UpdateDeltaTime();

  if (this->putPrompt && this->prompt != nullptr) {

    drawCanvas->RegisterCanva(this->prompt->Render, 2,

                              canvasPlace,

                              this->prompt);

    this->putPrompt = false;
  }

  const SDL_AppResult result =

      drawCanvas->RenderCanvas(gameContext->window, gameContext->render);

  UpdateLoadingPause(gameContext);

  if (gameContext->config.paused) {
    Pause();
  } else {
    Resume();
  }

  UpdateTriggers(GetDeltaTime());
  return result;
}

void Gameplay::Pause() { KotonohaTimePause(this->tm); }

void Gameplay::Resume() { KotonohaTimeResume(this->tm); }

void Gameplay::UpdateDeltaTime() {
  const Uint64 nowNs = SDL_GetTicksNS();

  if (lastFrameNs == 0) {
    lastFrameNs = nowNs;
    deltaTime = 0.0f;
    return;
  }

  const Uint64 elapsedNs = nowNs - lastFrameNs;
  lastFrameNs = nowNs;
  deltaTime = static_cast<float>(elapsedNs) / 1e9f;

  // Evita saltos enormes após pausas, breakpoints ou perda de foco.
  constexpr float kMaxDeltaTime = 0.25f;

  if (deltaTime > kMaxDeltaTime) {
    deltaTime = kMaxDeltaTime;
  }
}

float Gameplay::GetDeltaTime() const { return deltaTime; }

bool Gameplay::ShouldShowLoadingOverlay() const {
  return loadingPauseActive ||
         SDL_GetTicks() < loadingOverlayVisibleUntil;
}

void Gameplay::Reset(bool resetTime) {

#ifdef KOTONOHA_HAS_METALFX
  KotonohaMetalFXClearScalerCache();
#endif

  if (resetTime) {
    prefetchPendingPromotion = false;
    prefetchThreadSuspended = false;
    if (this->demuxer != nullptr) {
      KotonohaDemuxerStop(this->demuxer);
    }
    if (prompt != nullptr) {
      prompt->Reset();
    }
    drawCanvas->Reset();
    fade->Reset();
    firstFocus = true;
    KotonohaTimeReset(tm, true);
  }
  eventManager->Reset(this);
}

void Gameplay::SeekForward(Uint64 ms) { KotonohaTimeSeekForward(this->tm, ms); }

void Gameplay::SeekBackward(Uint64 ms) {
  KotonohaTimeSeekBackward(this->tm, ms);
  Reset(false);
}

float Gameplay::GetLastTime() {
  return MsToSeconds(static_cast<Uint64>(this->eventManager->lastTime));
}

float Gameplay::GetTime() { return MsToSeconds(KotonohaTimeGet(this->tm)); }

void Gameplay::SetTime(float time) {
  const float currentTime = GetTime();
  KotonohaTimeSet(this->tm, static_cast<Uint64>(time * 1000.0f));
  if (time < currentTime) {
    Reset(false);
  }
}

void Gameplay::SetMediaActive(bool active, bool prefetchOnly) {
  prefetchAllMedia.store(active, std::memory_order_release);
  mediaActive.store(active, std::memory_order_release);

  if (active && prefetchOnly) {
    prefetchPendingPromotion = firstFocus;
    if (!KotonohaTimeIsStarted(tm)) {
      KotonohaTimeStart(tm);
      KotonohaTimePause(tm);
    } else if (firstFocus && !KotonohaTimeIsPaused(tm)) {
      KotonohaTimePause(tm);
    }

    if (!KotonohaDemuxerIsStarted(demuxer)) {
      KotonohaDemuxerStart(demuxer);
    }
    prefetchThreadSuspended = false;
  } else if (active && prefetchThreadSuspended) {
    KotonohaDemuxerStart(demuxer);
    prefetchThreadSuspended = false;
  } else if (!active && !prefetchThreadSuspended) {
    KotonohaDemuxerSuspend(demuxer);
    prefetchThreadSuspended = true;
  }
}

bool Gameplay::IsMediaActive() const {
  return mediaActive.load(std::memory_order_acquire);
}

bool Gameplay::ShouldPrefetchAllMedia() const {
  return prefetchAllMedia.load(std::memory_order_acquire);
}

Gameplay::~Gameplay() {
  SetMediaActive(false);

  delete this->eventManager;
  delete this->video;
  delete this->image;
  delete this->audio;
  delete this->drawCanvas;
  delete this->fade;

  if (prompt) {
    delete prompt;
    prompt = nullptr;
  }

  if (demuxer != nullptr) {
    KotonohaDemuxerDestroy(&demuxer);
    demuxer = nullptr;
  }

  KotonohaTextRenderShutdown(&(this->sb));
  KotonohaTimeDestroy(this->tm);
}

} // namespace Kotonoha