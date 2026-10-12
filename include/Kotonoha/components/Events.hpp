#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Video.hpp>
#include <Kotonoha/components/VoiceActivity.hpp>

#include <unordered_map>
#include <vector>
extern "C" {
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/utils/IOMonitor.h>
#include <Kotonoha/utils/VoicePcm.h>
}

namespace Kotonoha {

struct VoiceAnimation {
  std::vector<VoiceActivityRecord> activity;
  MouthAnimationState state;
  Uint64 startTick = 0;
};

class Event {
private:
  struct SubtitleEventState {
    int index;
    long long start;
    long long duration;
  };

  Kotonoha_orsData eventsFromScript;
  static int EventManager(void *data);
  SDL_Mutex *eventMutex = nullptr;
  ASS_Track *subtitleTrack = nullptr;
  std::vector<SubtitleEventState> subtitleEvents;

  std::vector<Video *> videoToDelete;
  std::vector<Image *> imageToDelete;
  std::vector<Audio *> audioToDeleta;
  std::unordered_map<const Kotonoha_orsEvent *, VoiceAnimation> voiceAnimations;
  std::unordered_map<const Kotonoha_orsEvent *, Kotonoha_IOMonitorOperation>
      pendingAssetLoads;

  void SetSubtitlesActive(bool active);

public:
  Uint64 lastTime = 0;
  Event(const char *orsPath, void *gameplay, struct Kotonoha_Game *gameCtx);
  void ActivateSubtitles();
  void Reset(void* gameplay);
  bool CheckEnd(void *gameplay);
  ~Event();
};
} // namespace Kotonoha
