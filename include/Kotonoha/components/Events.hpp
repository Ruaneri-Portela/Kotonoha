#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Video.hpp>
#include <Kotonoha/components/VoiceActivity.hpp>

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/utils/VoicePcm.h>

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

  struct PendingAssetLoad {
    Uint64 retryAfterTicks = 0;
    bool loadingSignaled = false;
  };

  Kotonoha_OrsData eventsFromScript;
  static int EventManager(void *data);
  SDL_Mutex *eventMutex = nullptr;
  ASS_Track *subtitleTrack = nullptr;
  std::vector<SubtitleEventState> subtitleEvents;

  std::vector<Video *> videoToDelete;
  std::vector<Image *> imageToDelete;
  std::vector<Audio *> audioToDeleta;
  std::unordered_map<const Kotonoha_OrsEvent *, VoiceAnimation> voiceAnimations;
  std::unordered_map<const Kotonoha_OrsEvent *, PendingAssetLoad>
      pendingAssetLoads;
  std::unordered_set<const Kotonoha_OrsEvent *> liveEvents;

  void SetSubtitlesActive(bool active);

public:
  Uint64 lastTime = 0;
  Event(const char *orsPath, void *gameplay, Kotonoha_Game *gameCtx,
        TTF_Font *font);
  void ActivateSubtitles();
  void Reset(void *gameplay);
  bool CheckEnd(void *gameplay);
  std::vector<MediaBufferInfo>
  GetMediaBufferInfo(const Audio *audio, const Video *video,
                     const Image *image) const;
  bool PlayMediaNow(Audio *audio, const std::string &path,
                    const std::string &channel) const;
  ~Event();
};
} // namespace Kotonoha
