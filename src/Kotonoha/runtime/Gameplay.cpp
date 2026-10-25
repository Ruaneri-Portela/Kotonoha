#include <Kotonoha/Gameplay.hpp>
#include <Kotonoha/utils/Log.h>

#include <Kotonoha/specials/MetalFX.h>

#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <stdexcept>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace Kotonoha {
namespace {
static float MsToSeconds(Uint64 value) {
  return static_cast<float>(value) / 1000.0f;
}

class GameplayTaskLockGuard {
public:
  explicit GameplayTaskLockGuard(Kotonoha_Game *gameContext)
      : lock_(gameContext != nullptr ? gameContext->taskLock : nullptr) {
    if (lock_ != nullptr) SDL_LockMutex(lock_);
  }
  ~GameplayTaskLockGuard() {
    if (lock_ != nullptr) SDL_UnlockMutex(lock_);
  }

  GameplayTaskLockGuard(const GameplayTaskLockGuard &) = delete;
  GameplayTaskLockGuard &operator=(const GameplayTaskLockGuard &) = delete;

private:
  SDL_Mutex *lock_ = nullptr;
};
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
  const int renderScalePercent =
      gameContext_ != nullptr
          ? std::clamp(gameContext_->config.renderScalePercent.load(), 25, 100)
          : 100;
  const float renderScale = static_cast<float>(renderScalePercent) / 100.0f;
  drawCanvas->UpdateTargetSize(
      std::max(1, static_cast<int>(std::lround(place.w * renderScale))),
      std::max(1, static_cast<int>(std::lround(place.h * renderScale))));
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
                   TTF_Font *font,
                   GameplayEventRuntimeFactory eventFactory)
    : gameContext_(gameContext), eventRuntimeFactory(eventFactory) {
  if (gameContext == nullptr) {
    throw std::invalid_argument("Gameplay requires a game context");
  }
  try {
    this->demuxer = KotonohaDemuxerCreate(!gameContext->config.skipFiles,
                                          &gameContext->config);
    this->tm = KotonohaTimeNew(true);
    if (this->demuxer == nullptr || this->tm == nullptr) {
      throw std::runtime_error("Gameplay core timing/demuxer initialization failed");
    }
    this->sb = KotonohaTextRenderInit(this->tm, gameContext);
    if (this->sb == nullptr) {
      throw std::runtime_error("Gameplay subtitle initialization failed");
    }
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
    if (eventRuntimeFactory.create != nullptr) {
      ownedEvents = eventRuntimeFactory.create(
          this->scriptPath.c_str(), this, gameContext, font,
          eventRuntimeFactory.userdata);
    }
    this->eventManager = ownedEvents;
#if defined(__ANDROID__) || defined(ANDROID)
    playOnlyOnFocus = true;
    lastPauseStatus = gameContext->config.paused;
#endif
  } catch (...) {
    // A C++ constructor that throws never runs ~Gameplay.  Release the raw C
    // resources explicitly, after unwinding component owners that may still
    // reference them.
    ownedPrompt.reset();
    ownedCanvas.reset();
    ownedFade.reset();
    ownedAudio.reset();
    ownedImage.reset();
    ownedVideo.reset();
    drawCanvas = nullptr;
    fade = nullptr;
    audio = nullptr;
    image = nullptr;
    video = nullptr;
    if (sb != nullptr) KotonohaTextRenderShutdown(&sb);
    if (demuxer != nullptr) {
      KotonohaDemuxerDestroy(&demuxer);
      demuxer = nullptr;
    }
    if (tm != nullptr) {
      KotonohaTimeDestroy(tm);
      tm = nullptr;
    }
    throw;
  }
}

Gameplay::Gameplay(const char *scriptPath, Kotonoha_Game *gameContext,
                   GameplayEventRuntimeFactory eventFactory)
    : Gameplay(scriptPath, gameContext, nullptr, eventFactory) {}

SDL_AppResult Gameplay::Main(Kotonoha_Game *gameContext) {
  if (gameContext == nullptr || eventManager == nullptr ||
      drawCanvas == nullptr) {
    return SDL_APP_FAILURE;
  }
  if (eventManager->HasFatalError()) {
    KLOG_ERROR(CORE, "Gameplay event runtime entered a fatal state: %s",
               scriptPath.c_str());
    return SDL_APP_FAILURE;
  }
  // The physical drawable can change while a Gameplay stays alive (windowed /
  // fullscreen toggles, monitor moves, DPI changes, resolution changes). Keep
  // the authored canvas fitted to the current output instead of freezing the
  // first-frame placement for the whole scene.
  UpdateCanvasSize(gameContext->window, gameContext->render);

  // Prompt timeout/state belongs to the prompt behavior, not to rendering.
  // Advance it even when this frame ends before the prompt canvas is drawn.
  if (prompt != nullptr) {
    std::string presentationError;
    if (!prompt->PrepareBehavior(gameContext, &presentationError) &&
        !presentationError.empty()) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "Prompt behavior presentation unavailable: %s",
                  presentationError.c_str());
    }
    prompt->Update();
    prompt->PlayPendingAudio(audio);
  }
  if (firstFocus) {
    if (prefetchPendingPromotion) {
      KotonohaTimeStart(tm);
      prefetchPendingPromotion = false;
    } else if (!KotonohaTimeIsStarted(tm)) {
      KotonohaTimeStart(tm);
    }
    firstFocus = false;
    KotonohaDemuxerStart(this->demuxer);
    // Event construction occurs before this clock is started, so its eager
    // EventManager pass intentionally returns without dispatching tick zero.
    // Prime synchronously now; otherwise the 50 ms worker cadence can present
    // an empty black frame before the first CREATE_BG / movie is registered.
    if (!eventManager->ProcessNow(this, gameContext)) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "Failed to prime first scene frame: %s", scriptPath.c_str());
      if (eventManager->HasFatalError()) return SDL_APP_FAILURE;
    }
  } else if (eventManager->CheckEnd(this)) {
    if (loop) {
      Reset(true);
    } else if (prompt != nullptr &&
               (prompt->PresentationTailActive() ||
                prompt->PresentationNeedsFinalFrame())) {
      // A game-owned prompt behavior may retain a visual tail after committing
      // its result. Keep the scene alive until that behavior publishes its
      // terminal frame so transition capture cannot retain a stale overlay.
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
  if (eventManager->HasFatalError()) return SDL_APP_FAILURE;
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

void Gameplay::SetAutoChoiceMode(bool enabled) {
  autoChoiceMode.store(enabled, std::memory_order_release);
  if (prompt != nullptr) {
    prompt->SetAutoMode(enabled);
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

void Gameplay::ResetLocked(bool resetTime, bool suppressReplaySideEffects) {

#ifdef KOTONOHA_HAS_METALFX
  KotonohaMetalFXClearScalerCache();
#endif

  if (resetTime) {
    prefetchPendingPromotion = false;
    prefetchThreadSuspended = false;
    if (this->demuxer != nullptr) {
      KotonohaDemuxerStop(this->demuxer);
    }
    // A physical restart/seek must not erase a semantic choice that routing has
    // already committed.  The game runtime owns whether/how that committed
    // prompt is presented after its presentation rebuild.
    if (prompt != nullptr && !choiceCommitSubmitted) {
      prompt->Reset();
    }
    if (drawCanvas != nullptr) drawCanvas->Reset();
    if (fade != nullptr) fade->Reset();
    firstFocus = true;
    if (tm != nullptr) KotonohaTimeReset(tm, true);
  }
  if (eventManager != nullptr)
    eventManager->Reset(this, suppressReplaySideEffects);
}

void Gameplay::Reset(bool resetTime) {
  GameplayTaskLockGuard taskGuard(gameContext_);
  ResetLocked(resetTime);
}

void Gameplay::RestartCurrentScene() { Reset(true); }

void Gameplay::SeekForward(Uint64 ms) {
  if (tm == nullptr) return;
  bool moved = false;
  {
    GameplayTaskLockGuard taskGuard(gameContext_);
    Uint64 promptStart = 0;
    if (prompt != nullptr && prompt->PendingStartMs(&promptStart)) {
      const Uint64 now = KotonohaTimeGet(tm);
      if (now >= promptStart) return;
      // Compare the delta rather than now+ms so an oversized seek cannot wrap
      // Uint64 and accidentally jump past an unanswered choice boundary.
      if (ms >= promptStart - now) {
        KotonohaTimeSeekForward(tm, promptStart - now);
        moved = true;
      }
    }
    if (!moved) {
      KotonohaTimeSeekForward(tm, ms);
      moved = ms != 0;
    }
  }
  // The render thread observes the scene clock immediately.  Advance the event
  // runtime to the same destination before returning so media registration does
  // not trail the clock by the background worker's 50 ms cadence.
  if (moved && eventManager != nullptr &&
      !eventManager->ProcessNow(this, gameContext_)) {
    KLOG_ERROR(CORE, "Failed to advance presentation after forward seek: %s",
               scriptPath.c_str());
  }
}

void Gameplay::SeekBackward(Uint64 ms) {
  {
    GameplayTaskLockGuard taskGuard(gameContext_);
    KotonohaTimeSeekBackward(this->tm, ms);
    ResetLocked(false);
  }

  // Reset(false) clears/rebuilds presentation ownership at the new clock but
  // the game event runtime still has to activate the media that are live at
  // that destination.  Do it synchronously after releasing taskLock;
  // ProcessNow owns that lock itself.  Otherwise Canvas may render an empty
  // image/video set until the cadence-limited background worker runs.
  if (eventManager != nullptr &&
      !eventManager->ProcessNow(this, gameContext_)) {
    KLOG_ERROR(CORE, "Failed to rebuild presentation after backward seek: %s",
               scriptPath.c_str());
  }
}

bool Gameplay::SkipToEndBoundary() {
  if (tm == nullptr || eventManager == nullptr) return false;
  // EventManager runs on the shared process worker under taskLock.  Hold the
  // same lock for target computation + clock jump + presentation rebuild so a
  // worker pass can never observe the new clock with the old timeline/media
  // state in between.
  {
    GameplayTaskLockGuard taskGuard(gameContext_);
    const auto target = eventManager->ComputeSkipToEndTarget(this);
    if (!target.present) return false;
    Uint64 promptStart = 0;
    Uint64 targetMs = Kotonoha_SceneTickToMillisecondsCeil(target.tick);
    if (prompt != nullptr && prompt->PendingStartMs(&promptStart) &&
        targetMs > promptStart) {
      // An unanswered choice stops the skip at its start.
      if (KotonohaTimeGet(tm) >= promptStart) return false;
      targetMs = promptStart;
    }

    // Native state4 seek changes the scene clock as one transaction, then
    // reconstructs the active presentation at the destination.  Reusing the
    // Event reset avoids sequential dispatch of the skipped authored interval.
    KotonohaTimeSet(tm, targetMs);
    eventManager->Reset(this, true);
  }

  // Reset intentionally clears stale media before rebuilding state at the
  // destination.  A newly-created Gameplay after a higher-level script jump has not
  // started its demuxer yet; its first Main() already primes synchronously at
  // the sought clock, before any frame can be presented.  For an already-live
  // scene, prime here before returning to the render loop rather than leaving
  // a 0..50 ms window in which Canvas can present a black frame while waiting
  // for the background EventsThread cadence.
  if (firstFocus) return !eventManager->HasFatalError();
  if (!eventManager->ProcessNow(this, gameContext_)) {
    KLOG_ERROR(CORE, "Failed to rebuild presentation after skip: %s",
               scriptPath.c_str());
    return false;
  }
  return !eventManager->HasFatalError();
}

bool Gameplay::SetPlaybackRate(double requestedRate) {
  if (tm == nullptr || !KotonohaAudioRateIsSupported(requestedRate)) return false;
  GameplayTaskLockGuard taskGuard(gameContext_);
  const double previousRate = KotonohaTimeGetRate(tm);

  // Keep media and the scene clock on one rate. Game profiles decide which
  // rates to expose; the engine only applies the requested value.
  if (audio != nullptr && !audio->SetPlaybackRate(requestedRate)) {
    (void)audio->SetPlaybackRate(previousRate);
    return false;
  }
  if (!KotonohaTimeSetRate(tm, requestedRate)) {
    if (audio != nullptr) (void)audio->SetPlaybackRate(previousRate);
    return false;
  }
  return true;
}

float Gameplay::GetLastTime() {
  return eventManager != nullptr
             ? MsToSeconds(static_cast<Uint64>(eventManager->LastTime()))
             : 0.0f;
}

float Gameplay::GetTime() { return MsToSeconds(KotonohaTimeGet(this->tm)); }

void Gameplay::SetTime(float time) {
  if (tm == nullptr || !std::isfinite(time)) return;
  const double requestedMsDouble =
      static_cast<double>(SDL_max(time, 0.0f)) * 1000.0;
  const Uint64 requestedMs = requestedMsDouble >= static_cast<double>(SDL_MAX_UINT64)
                                 ? SDL_MAX_UINT64
                                 : static_cast<Uint64>(requestedMsDouble);
  bool changed = false;
  {
    GameplayTaskLockGuard taskGuard(gameContext_);
    const Uint64 currentMs = KotonohaTimeGet(tm);
    changed = requestedMs != currentMs;
    if (changed) {
      // Absolute editor/debug seeks are discontinuities, not playback.  Rebuild
      // the timeline at the destination instead of dispatching every event in
      // the skipped interval; otherwise authored history/side effects can be
      // replayed for time that was never actually presented.
      KotonohaTimeSet(tm, requestedMs);
      ResetLocked(false, true);
    }
  }
  if (changed && eventManager != nullptr &&
      !eventManager->ProcessNow(this, gameContext_)) {
    KLOG_ERROR(CORE, "Failed to rebuild presentation after SetTime: %s",
               scriptPath.c_str());
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

  if (ownedEvents != nullptr) {
    if (eventRuntimeFactory.destroy != nullptr) {
      eventRuntimeFactory.destroy(ownedEvents, eventRuntimeFactory.userdata);
    } else {
      delete ownedEvents;
    }
    ownedEvents = nullptr;
  }
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
