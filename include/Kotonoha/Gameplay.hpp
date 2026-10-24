#pragma once

#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/components/Fade.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/components/Video.hpp>
#include <Kotonoha/utils/Demuxer.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <atomic>
#include <memory>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/utils/Time.h>

namespace Kotonoha {

class GameplayPauseIntent {
public:
  void BeginLoading(bool currentUserPaused) {
    if (loading_) return;
    userPaused_ = currentUserPaused;
    loading_ = true;
  }

  void EndLoading() { loading_ = false; }
  void SetUserPaused(bool paused) { userPaused_ = paused; }

  bool UserPaused() const { return userPaused_; }
  bool Loading() const { return loading_; }
  bool EffectivePaused() const { return loading_ || userPaused_; }

private:
  bool userPaused_ = false;
  bool loading_ = false;
};

class Gameplay {
private:
  float aspectRatio = 16.0f / 9.0f;
  int windowWidth = 0, windowHeight = 0;
  SDL_FRect canvasPlace = {0, 0, 0, 0};
  bool lastPauseStatus = false, playOnlyOnFocus = false;
  GameplayPauseIntent pauseIntent{};
  bool loadingCandidateActive = false;

  Uint64 loadingCandidateStartedAt = 0;
  Uint64 loadingOverlayVisibleUntil = 0;
  Uint64 loadingPauseStartedAt = 0;
  float deltaTime = 0.0f;
  Uint64 lastFrameNs = 0;

  std::atomic_bool mediaActive{false};
  std::atomic_bool shutdownRequested{false};
  std::atomic_bool autoChoiceMode{false};
  std::atomic_bool prefetchAllMedia{false};
  bool prefetchPendingPromotion = false;
  bool prefetchThreadSuspended = false;
  GameplayEventRuntimeFactory eventRuntimeFactory{};
  void UpdateDeltaTime();
  void UpdateCanvasSize(SDL_Window *window, SDL_Renderer *renderer);
  void UpdateLoadingPause(struct Kotonoha_Game *gameContext);

  struct TriggerSeekState {
    Sint16 value = 0;
    bool active = false;
  };

  TriggerSeekState leftTrigger;
  TriggerSeekState rightTrigger;
  void SetLeftTrigger(Sint16 value);
  void SetRightTrigger(Sint16 value);
  void UpdateTriggers(float deltaTimeSeconds);
  void HandleTriggerSeek(TriggerSeekState &state, float deltaTimeSeconds,
                         bool backward);

  void HandleGamepadButton(SDL_Event *event, Kotonoha_Game *gameContext);
  void HandleGamepadAxis(SDL_Event *event, Kotonoha_Game *gameContext);
  void HandleKeyboard(SDL_Event *event, Kotonoha_Game *gameContext);
  void HandleWindowEvent(SDL_Event *event, Kotonoha_Game *gameContext);
  void SeekByKeyboardN(Kotonoha_Game *gameContext);
  void SeekByKeyboardB(Kotonoha_Game *gameContext);

#if defined(KOTONOHA_MOBILE)
  void HandleMobileFocus(SDL_Event *event, Kotonoha_Game *gameContext);
#endif

public:
  Kotonoha_Subtitles *sb = nullptr;
  Kotonoha_Time *tm = nullptr;
  Kotonoha_Demuxer *demuxer = nullptr;
  Canvas *drawCanvas = nullptr;
  GameplayEventRuntime *eventManager = nullptr;
  Video *video = nullptr;
  Image *image = nullptr;
  Audio *audio = nullptr;
  Fade *fade = nullptr;
  Prompt *prompt = nullptr;
  // Owners of the observer pointers above.
  std::unique_ptr<Canvas> ownedCanvas;
  GameplayEventRuntime *ownedEvents = nullptr;
  std::unique_ptr<Video> ownedVideo;
  std::unique_ptr<Image> ownedImage;
  std::unique_ptr<Audio> ownedAudio;
  std::unique_ptr<Fade> ownedFade;
  std::unique_ptr<Prompt> ownedPrompt;
  int promptId = -2;
  bool putPrompt = false;
  bool choiceCommitSubmitted = false;
  bool reset = false, back = false, hardReset = false, loop = false,
       firstFocus = true;
  std::string scriptPath;
  Gameplay(const char *scriptPath, struct Kotonoha_Game *gameContext,
           TTF_Font *font, GameplayEventRuntimeFactory eventFactory);
  Gameplay(const char *scriptPath, struct Kotonoha_Game *gameContext,
           GameplayEventRuntimeFactory eventFactory);
  SDL_AppResult Main(struct Kotonoha_Game *gameContext);
  void Events(SDL_Event *event, struct Kotonoha_Game *gameContext);
  void Reset(bool resetTime);
  void RestartCurrentScene();
  void Pause();
  void Resume();
  void SetUserPaused(struct Kotonoha_Game *gameContext, bool paused);
  void ToggleUserPaused(struct Kotonoha_Game *gameContext);
  bool UserPaused(const struct Kotonoha_Game *gameContext) const;
  void SetAutoChoiceMode(bool enabled);
  bool AutoChoiceMode() const {
    return autoChoiceMode.load(std::memory_order_acquire);
  }

  void SeekForward(Uint64 ms);
  void SeekBackward(Uint64 ms);
  bool SkipToEndBoundary();
  bool SetPlaybackRate(double rate);
  float GetDeltaTime() const;
  bool ShouldShowLoadingOverlay() const;
  float GetTime();
  float GetLastTime();
  void SetTime(float time);
  void SetMediaActive(bool active, bool prefetchOnly = false);
  bool IsMediaActive() const;
  bool ShouldPrefetchAllMedia() const;
  void RequestShutdown();
  bool ShutdownRequested() const {
    return shutdownRequested.load(std::memory_order_acquire);
  }
  const std::atomic_bool *ShutdownFlag() const { return &shutdownRequested; }
  ~Gameplay();
};
} // namespace Kotonoha
