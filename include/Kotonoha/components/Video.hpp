#pragma once

#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/components/Upscaling.hpp>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/Time.h>

namespace Kotonoha {

class Video : public Upscaling {
private:
  Kotonoha_Time *timeManager = nullptr;
  Kotonoha_Demuxer *demuxer = nullptr;

  SDL_Texture *heldTexture = nullptr;
  SDL_Mutex *lock = nullptr;

  std::vector<Kotonoha_VideoData *> videos =
      std::vector<Kotonoha_VideoData *>();

  void RenderTexture(SDL_Renderer *render, SDL_Texture *texture,
                     Uint64 contentKey);

public:
  void Reset();
  std::vector<MediaBufferInfo> GetBufferInfo() const;

  Video(Kotonoha_Time *timeManager, Kotonoha_Demuxer *demuxer);

  bool Register(const char *path, Uint64 startTime, Uint64 endTime,
                bool useOrsFrameTimeline = false, Uint64 orsEndFrame = 0);

  static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  ~Video();
};

} // namespace Kotonoha
