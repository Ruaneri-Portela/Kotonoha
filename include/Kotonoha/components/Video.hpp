#pragma once

#include <Kotonoha/components/Upscaling.hpp>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/Time.h>

namespace Kotonoha {

class Video : public Upscaling {
private:
  float cacheDurationSeconds = 2.0f;
  Kotonoha_Time *timeManager = nullptr;
  Kotonoha_IOMonitor *ioMonitor = nullptr;
  Kotonoha_Demuxer *demuxer = nullptr;

  SDL_Texture *heldTexture = nullptr;
  SDL_Mutex *lock = nullptr;

  std::vector<Kotonoha_VideoData *> videos =
      std::vector<Kotonoha_VideoData *>();

  void RenderTexture(SDL_Renderer *render, SDL_Texture *texture);

public:
  void Reset();

  Video(Kotonoha_Time *timeManager, Kotonoha_IOMonitor *ioMonitor,
        Kotonoha_Demuxer *demuxer, float cacheDurationSeconds = 2.0f);

  bool Register(const char *path, Uint64 startTime, Uint64 endTime,
                bool useOrsFrameTimeline = false, Uint64 orsEndFrame = 0);

  static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  ~Video();
};

} // namespace Kotonoha
