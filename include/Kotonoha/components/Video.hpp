#pragma once
#include <vector>

extern "C" {
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/Time.h>
#include <Kotonoha/utils/UserEvents.h>

}

namespace Kotonoha {
class Video {
private:
  Kotonoha_time *timeManager = nullptr;
  struct Item {
    const Kotonoha_orsEvent* event = nullptr;
    Kotonoha_videoData* decoder = nullptr;
    bool active = false;
  };
  std::vector<Item> videos;
  SDL_Mutex *lock = nullptr;

public:
  void Reset();

  Video(Kotonoha_time *timeManager);

  bool Prepare(const Kotonoha_orsEvent* event, const char* path);
  bool Activate(const Kotonoha_orsEvent* event);
  bool IsEof(const Kotonoha_orsEvent* event);
  void Remove(const Kotonoha_orsEvent* event);

  static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  ~Video();
};
} // namespace Kotonoha
