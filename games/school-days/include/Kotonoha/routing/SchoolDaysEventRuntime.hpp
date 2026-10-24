#pragma once

#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Video.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/SchoolDaysAudioEventState.hpp>
#include <Kotonoha/SchoolDaysBgmEventState.hpp>
#include <Kotonoha/graphics/AbcAnimation.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/SchoolDaysRemainingOrs.hpp>
#include <SDL3_ttf/SDL_ttf.h>

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/renders/TextRender.h>
}

namespace Kotonoha {

class SchoolDaysEventRuntime final : public GameplayEventRuntime {
private:
  Kotonoha_orsData eventsFromScript;
  SchoolDaysSceneTimeline timeline;
  SchoolDaysOptionalTick nextBoundaryTick;
  SchoolDaysSeSlots seSlots;
  SchoolDaysBgmSlots bgmSlots;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> voiceMedia;
  std::unordered_map<const Kotonoha_orsEvent*,
                     std::vector<AbcActivityRecord>> voiceActivity;
  struct AbcBinding {
    const Kotonoha_orsEvent* voice = nullptr;
    Image::AbcGroup* group = nullptr;
    Uint64 ownerBaseGeneration = 0;
    AbcAnimationState state;
    Kotonoha_SceneTick lastTick = 0;
    bool initialized = false;
  };
  std::unordered_map<std::string, AbcBinding> abcBindings;
  std::vector<const Kotonoha_orsEvent*> activeVoiceEvents;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> seMedia;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> normalBgmMedia;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> endingBgmMedia;
  std::unordered_map<const Kotonoha_orsEvent*, SchoolDaysMovieEventState> movieStates;
  std::unordered_map<const Kotonoha_orsEvent*, Uint64> bgResources;
  SchoolDaysMoveSomNoOp moveSom;
  static int EventManager(void *data);
  SDL_Mutex *eventMutex = nullptr;
  std::atomic_bool readyToEnd{false};
  std::atomic_bool setSelectReached{false};
  std::atomic_bool suppressBackLogCaptureOnce{false};
  TTF_Font *font = nullptr;

  std::vector<Video*> videoToDelete;
  std::vector<Image*> imageToDelete;
  std::vector<Audio*> audioToDeleta;

public:
  Uint64 lastTime = 0;
  SchoolDaysEventRuntime(const char *orsPath, void *gameplay,
                         Kotonoha_Game *gameCtx, TTF_Font *font = nullptr);
  bool ProcessNow(void *gameplay, Kotonoha_Game *gameCtx) override;
  void Reset(void *gameplay, bool suppressBackLogCapture = false) override;
  GameplayEventTarget ComputeSkipToEndTarget(void *gameplay) const override;
  bool SetSelectReached() const override {
    return setSelectReached.load(std::memory_order_acquire);
  }
  bool ReachedNext(void *gameplay) const override;
  bool CheckEnd(void *gameplay) override;
  Uint64 LastTime() const override { return lastTime; }
  std::vector<MediaBufferInfo> GetMediaBufferInfo(
      const Audio *audio, const Video *video, const Image *image) const override;
  bool PlayMediaNow(Audio *audio, const std::string &path,
                    const std::string &channel) const override;
  void StopMediaNow(Audio *audio) const override;
  std::string GetMediaNowPath(Audio *audio) const override;
  ~SchoolDaysEventRuntime() override;
};

GameplayEventRuntimeFactory MakeSchoolDaysEventRuntimeFactory();

} // namespace Kotonoha
