#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Video.hpp>
extern "C" {
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/renders/TextRender.h>
}

namespace Kotonoha {
class Event {
private:
  Kotonoha_orsData eventsFromScript;
  bool inExit = false;
  bool closed = false;
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
