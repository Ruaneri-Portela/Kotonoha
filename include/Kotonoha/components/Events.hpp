#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Video.hpp>
#include <Kotonoha/SchoolDaysSceneTimeline.hpp>
#include <Kotonoha/SchoolDaysAudioEventState.hpp>
#include <Kotonoha/SchoolDaysBgmEventState.hpp>
#include <Kotonoha/SchoolDaysAbcRuntime.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
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
  SchoolDaysSeSlots seSlots;
  SchoolDaysBgmSlots bgmSlots;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> voiceMedia;
  std::unordered_map<const Kotonoha_orsEvent*,
                     std::vector<SchoolDaysActivityRecord>> voiceActivity;
  struct AbcBinding {
    const Kotonoha_orsEvent* voice = nullptr;
    Image::AbcGroup* group = nullptr;
    SchoolDaysAbcState state;
    Kotonoha_SceneTick lastTick = 0;
    bool initialized = false;
  };
  std::unordered_map<std::string, AbcBinding> abcBindings;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> seMedia;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> normalBgmMedia;
  std::unordered_map<const Kotonoha_orsEvent*, Kotonoha_audioDecode*> endingBgmMedia;
  std::unordered_map<const Kotonoha_orsEvent*, SchoolDaysMovieEventState> movieStates;
  static int EventManager(void *data);
  SDL_Mutex *eventMutex = nullptr;

  std::vector<Video*> videoToDelete;
  std::vector<Image*> imageToDelete;
  std::vector<Audio*> audioToDeleta;

public:
  Uint64 lastTime = 0;
  Event(const char *orsPath, void *gameplay, struct Kotonoha_Game *gameCtx);
  void Reset(void* gameplay);
  bool CheckEnd(void *gameplay);
  ~Event();
};
} // namespace Kotonoha
