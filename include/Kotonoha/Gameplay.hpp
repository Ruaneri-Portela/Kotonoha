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

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/utils/Time.h>

namespace Kotonoha {
class Gameplay {
private:
  float aspectRatio = 16.0f / 9.0f;
  int windowWidth = 0, windowHeight = 0;
  SDL_FRect canvasPlace = {0, 0, 0, 0};
  bool lastPauseStatus = false, playOnlyOnFocus = false;
  bool loadingPauseActive = false, pausedBeforeLoading = false;
  bool loadingCandidateActive = false;

  Uint64 loadingCandidateStartedAt = 0;
  Uint64 loadingOverlayVisibleUntil = 0;
  Uint64 loadingPauseStartedAt = 0;
  float deltaTime = 0.0f;
  Uint64 lastFrameNs = 0;

  std::atomic_bool mediaActive{false};
  std::atomic_bool prefetchAllMedia{false};
  bool prefetchPendingPromotion = false;
  bool prefetchThreadSuspended = false;
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
  Event *eventManager = nullptr;
  Video *video = nullptr;
  Image *image = nullptr;
  Audio *audio = nullptr;
  Fade *fade = nullptr;
  Prompt *prompt = nullptr;
  int promptId = -1;
  bool putPrompt = false;
  bool reset = false, back = false, hardReset = false, loop = false,
       firstFocus = true;
  std::string scriptPath;
  Gameplay(const char *scriptPath, struct Kotonoha_Game *gameContext,
           TTF_Font *font);
  SDL_AppResult Main(struct Kotonoha_Game *gameContext);
  void Events(SDL_Event *event, struct Kotonoha_Game *gameContext);
  void Reset(bool resetTime);
  void Pause();
  void Resume();

  void SeekForward(Uint64 ms);
  void SeekBackward(Uint64 ms);
  float GetDeltaTime() const;
  bool ShouldShowLoadingOverlay() const;
  float GetTime();
  float GetLastTime();
  void SetTime(float time);
  void SetMediaActive(bool active, bool prefetchOnly = false);
  bool IsMediaActive() const;
  bool ShouldPrefetchAllMedia() const;
  ~Gameplay();
};
} // namespace Kotonoha
