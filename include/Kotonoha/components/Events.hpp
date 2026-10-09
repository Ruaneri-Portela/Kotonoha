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
}

namespace Kotonoha {
class Event {
private:
  Kotonoha_orsData eventsFromScript;
  static int EventManager(void *data);
  SDL_Mutex *eventMutex = nullptr;

  std::vector<Video*> videoToDelete;
  std::vector<Image*> imageToDelete;
  std::vector<Audio*> audioToDeleta;
  struct VoiceAnimation {
    std::vector<VoiceActivityRecord> activity;
    MouthAnimationState state;
    Uint64 startTick = 0;
  };
  std::unordered_map<const Kotonoha_orsEvent*, VoiceAnimation> voiceAnimations;

public:
  Uint64 lastTime = 0;
  Event(const char *orsPath, void *gameplay, struct Kotonoha_Game *gameCtx);
  void Reset(void* gameplay);
  bool CheckEnd(void *gameplay);
  ~Event();
};
} // namespace Kotonoha
