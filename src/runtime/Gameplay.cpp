#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/utils/Log.h>

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
      KLOG_ERROR(CORE, "SDL_GetCurrentRenderOutputSize failed: %s",
                 SDL_GetError());
      return;
    }
  } else if (!SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH)) {
    KLOG_ERROR(CORE, "SDL_GetWindowSizeInPixels failed: %s", SDL_GetError());
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
  const Uint64 kLoadingDebounceMs =
      static_cast<Uint64>(gameContext->config.loadingDebounceMs);
  const Uint64 kMinimumLoadingPauseMs =
      static_cast<Uint64>(gameContext->config.minLoadingPauseMs);

  if (loading) {
    if (!loadingCandidateActive) {
      loadingCandidateActive = true;
      loadingCandidateStartedAt = now;
    }
    if (now - loadingCandidateStartedAt < kLoadingDebounceMs) {
      return;
    }

    if (!pauseIntent.Loading()) {
      pauseIntent.BeginLoading(gameContext->config.paused);
      loadingPauseStartedAt = now;
    }
    loadingOverlayVisibleUntil = now + kMinimumLoadingPauseMs;
    gameContext->config.paused = pauseIntent.EffectivePaused();
  } else {
    loadingCandidateActive = false;
    loadingCandidateStartedAt = 0;
    if (pauseIntent.Loading() && now >= loadingOverlayVisibleUntil &&
        now - loadingPauseStartedAt >= kMinimumLoadingPauseMs) {
      pauseIntent.EndLoading();
      gameContext->config.paused = pauseIntent.EffectivePaused();
      loadingPauseStartedAt = 0;
    }
  }
}

Gameplay::Gameplay(const char *scriptPath, Kotonoha_Game *gameContext,
                   TTF_Font *font) {
  this->demuxer = KotonohaDemuxerCreate(!gameContext->config.skipFiles,
                                        &gameContext->config);
  this->tm = KotonohaTimeNew(true);
  this->sb = KotonohaTextRenderInit(this->tm, gameContext);
  ownedVideo = std::make_unique<Video>(this->tm, this->demuxer);
  ownedImage = std::make_unique<Image>(this->tm, gameContext->config.skipFiles);
  ownedAudio = std::make_unique<Audio>(static_cast<Sound *>(gameContext->sound),
                                       this->tm, this->demuxer);
  ownedFade = std::make_unique<Fade>(this->tm);
  ownedCanvas = std::make_unique<Canvas>();
  this->video = ownedVideo.get();
  this->image = ownedImage.get();
  this->audio = ownedAudio.get();
  this->fade = ownedFade.get();
  this->drawCanvas = ownedCanvas.get();

  this->drawCanvas->RegisterCanva(
      this->image->Render, 0,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->image, "Image");

  this->drawCanvas->RegisterCanva(
      this->video->Render, 1,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->video, "Video");

  this->drawCanvas->RegisterCanva(
      KotonohaTextRenderDraw, 3,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->sb, "Subtitles");

  this->drawCanvas->RegisterCanva(
      this->fade->Render, 4,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->fade, "Fade");

  this->scriptPath = scriptPath ? scriptPath : "";
  ownedEvents =
      std::make_unique<Event>(this->scriptPath.c_str(), this, gameContext, font);
  this->eventManager = ownedEvents.get();
#if defined(__ANDROID__) || defined(ANDROID)
  playOnlyOnFocus = true;
  lastPauseStatus = gameContext->config.paused;
#endif
}

Gameplay::Gameplay(const char *scriptPath, Kotonoha_Game *gameContext)
    : Gameplay(scriptPath, gameContext, nullptr) {}

SDL_AppResult Gameplay::Main(Kotonoha_Game *gameContext) {
  if (gameContext == nullptr || eventManager == nullptr ||
      drawCanvas == nullptr) {
    return SDL_APP_FAILURE;
  }
  /* Choice timeout is timeline state, not a rendering side effect.  Keep the
   * School Days -2 (pending) -> -1 (timeout) transition advancing even when a
   * frame ends before the prompt canvas is rendered. */
  if (prompt != nullptr) {
    if (prompt->UsesSchoolDaysPolicy()) {
      std::string presentationError;
      const auto presentationRoot = SelectSchoolDaysAssetRoot(
          gameContext->profileAssetsRoot, gameContext->config.assetsPath);
      if (!prompt->ConfigureSchoolDaysPresentation(
              presentationRoot.root, &presentationError) &&
          !presentationError.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "School Days choice presentation unavailable: %s",
                    presentationError.c_str());
      }
    }
    prompt->Update();
    prompt->PlaySchoolDaysPendingAudio(audio);
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
    } else if (prompt != nullptr &&
               (prompt->SchoolDaysPresentationTailActive() ||
                prompt->SchoolDaysPresentationNeedsFinalFrame())) {
      // School Days commits the choice result before its state2/state3 exit
      // animation finishes. Keep rendering through the exact alpha-zero frame
      // at tick 24/48 so a scene-transaction lease cannot retain the prior
      // alpha-11 composition.
      gameContext->config.next = false;
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

                              this->prompt, "Prompt");

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

void Gameplay::SetUserPaused(struct Kotonoha_Game *gameContext, bool paused) {
  if (gameContext == nullptr) return;

  pauseIntent.SetUserPaused(paused);
  gameContext->config.paused = pauseIntent.EffectivePaused();
  if (gameContext->config.paused) {
    Pause();
  } else {
    Resume();
  }
}

void Gameplay::ToggleUserPaused(struct Kotonoha_Game *gameContext) {
  if (gameContext == nullptr) return;
  SetUserPaused(gameContext, !UserPaused(gameContext));
}

bool Gameplay::UserPaused(const struct Kotonoha_Game *gameContext) const {
  if (pauseIntent.Loading()) return pauseIntent.UserPaused();
  return gameContext != nullptr ? gameContext->config.paused.load()
                                : pauseIntent.UserPaused();
}

void Gameplay::SetSchoolDaysAutoMode(bool enabled) {
  schoolDaysAutoMode.store(enabled, std::memory_order_release);
  if (prompt != nullptr) {
    prompt->SetSchoolDaysAutoMode(enabled);
  }
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
  deltaTime = static_cast<float>(elapsedNs) / 1e9f;

  // Evita saltos enormes após pausas, breakpoints ou perda de foco.
  constexpr float kMaxDeltaTime = 0.25f;

  if (deltaTime > kMaxDeltaTime) {
    deltaTime = kMaxDeltaTime;
  }
}

float Gameplay::GetDeltaTime() const { return deltaTime; }

bool Gameplay::ShouldShowLoadingOverlay() const {
  return pauseIntent.Loading() || SDL_GetTicks() < loadingOverlayVisibleUntil;
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

void Gameplay::RestartCurrentScene() { Reset(true); }

void Gameplay::SeekForward(Uint64 ms) {
  Uint64 promptStart = 0;
  if (prompt != nullptr && prompt->PendingStartMs(&promptStart)) {
    const Uint64 now = KotonohaTimeGet(tm);
    if (now >= promptStart) return;
    if (now + ms >= promptStart) {
      KotonohaTimeSeekForward(tm, promptStart - now);
      return;
    }
  }
  KotonohaTimeSeekForward(tm, ms);
}

void Gameplay::SeekBackward(Uint64 ms) {
  KotonohaTimeSeekBackward(this->tm, ms);
  Reset(false);
}

bool Gameplay::SkipSchoolDaysToEnd() {
  if (tm == nullptr || eventManager == nullptr) return false;
  const SchoolDaysOptionalTick target = eventManager->ComputeSkipToEndTarget(this);
  if (!target) return false;
  Uint64 promptStart = 0;
  Uint64 targetMs = Kotonoha_SceneTickToMillisecondsCeil(*target);
  if (prompt != nullptr && prompt->PendingStartMs(&promptStart) &&
      targetMs > promptStart) {
    // An unanswered choice stops the skip at its start.
    if (KotonohaTimeGet(tm) >= promptStart) return false;
    targetMs = promptStart;
  }

  // Native state4 seek changes the scene clock as one transaction, then
  // reconstructs the active presentation at the destination.  Reusing the
  // Event reset path avoids sequential dispatch of the skipped ORS interval.
  KotonohaTimeSet(tm, targetMs);
  eventManager->Reset(this, true);
  return true;
}

bool Gameplay::SetSchoolDaysPlaybackSpeedIndex(int index) {
  static constexpr double kNativeRates[] = {1.0, 2.0, 4.0, 12.0, 24.0};
  if (tm == nullptr || index < 0 || index >= 5) return false;
  const double previousRate = KotonohaTimeGetRate(tm);
  const double requestedRate = kNativeRates[index];

  // Retail broadcasts the rate to active audio/voice and advances the FILM
  // clock with the same factor. Video reads this shared clock directly.
  if (audio != nullptr && !audio->SetSchoolDaysPlaybackRate(requestedRate)) {
    (void)audio->SetSchoolDaysPlaybackRate(previousRate);
    return false;
  }
  if (!KotonohaTimeSetRate(tm, requestedRate)) {
    if (audio != nullptr) (void)audio->SetSchoolDaysPlaybackRate(previousRate);
    return false;
  }
  return true;
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

void Gameplay::RequestShutdown() {
  shutdownRequested.store(true, std::memory_order_release);
  if (demuxer != nullptr) KotonohaDemuxerStop(demuxer);
}

Gameplay::~Gameplay() {
  RequestShutdown();
  mediaActive.store(false, std::memory_order_release);

  ownedEvents.reset();
  ownedVideo.reset();
  ownedImage.reset();
  ownedAudio.reset();
  ownedCanvas.reset();
  ownedFade.reset();
  ownedPrompt.reset();
  eventManager = nullptr;
  video = nullptr;
  image = nullptr;
  audio = nullptr;
  drawCanvas = nullptr;
  fade = nullptr;
  prompt = nullptr;

  if (demuxer != nullptr) {
    KotonohaDemuxerDestroy(&demuxer);
    demuxer = nullptr;
  }

  KotonohaTextRenderShutdown(&(this->sb));
  KotonohaTimeDestroy(this->tm);
}

} // namespace Kotonoha
