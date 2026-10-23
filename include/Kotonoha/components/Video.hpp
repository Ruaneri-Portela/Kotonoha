#pragma once

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/renders/VideoRender.h>
#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/Time.h>
#include <vector>

namespace Kotonoha {

class Video {
private:
  Kotonoha_time *timeManager = nullptr;
  Kotonoha_Demuxer *demuxer = nullptr;

  struct Item {
    const Kotonoha_orsEvent *event = nullptr;
    Kotonoha_videoData *decoder = nullptr;
    bool active = false;
  };

  std::vector<Item> videos;
  std::vector<Kotonoha_videoData *> retired;
  SDL_Mutex *lock = nullptr;

  Kotonoha_videoData *CreateDecode(const Kotonoha_orsEvent *event,
                                   const char *path);

public:
  Video(Kotonoha_time *timeManager);
  Video(Kotonoha_time *timeManager, Kotonoha_Demuxer *demuxer);

  /* PREPARE creates/cache-primes the decoder but does not make it drawable. */
  bool Prepare(const Kotonoha_orsEvent *event, const char *path);
  bool Activate(const Kotonoha_orsEvent *event);
  bool IsEof(const Kotonoha_orsEvent *event);
  void Remove(const Kotonoha_orsEvent *event);
  void CollectRetired();
  void Reset();
  std::vector<MediaBufferInfo> GetBufferInfo() const;

  static Kotonoha_Scene_Status RenderCallback(KOTONOHA_SCENE_CALL);

  /*
   * A callable callback token keeps both source contracts without creating an
   * overloaded function set.  Gameplay historically writes
   * `video->Render` where Canvas expects a four-argument function pointer,
   * while the frozen G2.7 helper invokes `Video::Render(...)` with the removed
   * event-stack argument. Video never consumed that event stack.
   */
  struct RenderDispatcher {
    using Callback = Kotonoha_Scene_Status (*)(SDL_Window *, SDL_Renderer *,
                                               void *, SDL_Texture *);

    operator Callback() const { return &Video::RenderCallback; }

    Kotonoha_Scene_Status operator()(SDL_Window *window, SDL_Renderer *render,
                                     void *userData, SDL_Texture *target) const {
      return Video::RenderCallback(window, render, userData, target);
    }

    Kotonoha_Scene_Status operator()(SDL_Window *window, SDL_Renderer *render,
                                     void *legacyEventQueue, void *userData,
                                     SDL_Texture *target) const {
      (void)legacyEventQueue;
      return Video::RenderCallback(window, render, userData, target);
    }
  };

  inline static const RenderDispatcher Render{};
  ~Video();
};

} // namespace Kotonoha
