#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Video.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/SchoolDaysAudioEventState.hpp>
#include <Kotonoha/SchoolDaysBgmEventState.hpp>
#include <Kotonoha/SchoolDaysAbcRuntime.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/SchoolDaysRemainingOrs.hpp>
#include <SDL3_ttf/SDL_ttf.h>
#include <atomic>
#include <unordered_map>
extern "C" {
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/renders/TextRender.h>
}

namespace Kotonoha {
class Event {
private:
  Kotonoha_orsData eventsFromScript;
  SchoolDaysSceneTimeline timeline;
  SchoolDaysOptionalTick nextBoundaryTick;
  SchoolDaysSeSlots seSlots;
  SchoolDaysBgmSlots bgmSlots;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> voiceMedia;
  std::unordered_map<const Kotonoha_orsEvent*,
                     std::vector<SchoolDaysActivityRecord>> voiceActivity;
  struct AbcBinding {
    const Kotonoha_orsEvent* voice = nullptr;
    Image::AbcGroup* group = nullptr;
    Uint64 ownerBaseGeneration = 0;
    SchoolDaysAbcState state;
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
  Event(const char *orsPath, void *gameplay, struct Kotonoha_Game *gameCtx,
        TTF_Font *font = nullptr);
  void ActivateSubtitles() {}
  void Reset(void* gameplay, bool suppressBackLogCapture = false);
  SchoolDaysOptionalTick ComputeSkipToEndTarget(void* gameplay) const;
  bool SetSelectReached() const {
    return setSelectReached.load(std::memory_order_acquire);
  }
  bool ReachedNext(void *gameplay) const;
  bool CheckEnd(void *gameplay);
  std::vector<MediaBufferInfo> GetMediaBufferInfo(const Audio *audio,
                                                  const Video *video,
                                                  const Image *image) const;
  bool PlayMediaNow(Audio *audio, const std::string &path,
                    const std::string &channel) const;
  void StopMediaNow(Audio *audio) const;
  std::string GetMediaNowPath(Audio *audio) const;
  ~Event();
};
} // namespace Kotonoha
