#pragma once
#include <Kotonoha/SchoolDaysAbcRuntime.hpp>
#include <Kotonoha/components/MediaBufferInfo.hpp>
#if defined(KOTONOHA_HAS_ANIME4K) || defined(KOTONOHA_HAS_METALFX)
#include <Kotonoha/components/Upscaling.hpp>
#endif
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/utils/Time.h>
}

namespace Kotonoha {
class Image
#if defined(KOTONOHA_HAS_ANIME4K) || defined(KOTONOHA_HAS_METALFX)
    : public Upscaling
#endif
{
public:
  struct AbcGroup {
    SchoolDaysAbcGroupResources resources;
    Uint64 ownerBaseGeneration = 0;
    Uint64 startTime = 0, endTime = 0;
    SDL_Texture *textures[3] = {nullptr, nullptr, nullptr};
    Kotonoha_ImageLoad loads[3] = {};
    Uint8 activeIndex = 0;
    bool bound = false;
    Uint64 bindingStartTime = 0, bindingEndTime = 0;

    ~AbcGroup();
  };

private:
  Kotonoha_time *timeManager = nullptr;
  bool ignoreMissingAssets = false;
  std::vector<Kotonoha_Picture *> pictures;
  SDL_Mutex *lock = nullptr;
  std::vector<std::unique_ptr<AbcGroup>> abcGroups;
  Uint64 nextBaseGeneration = 1;
  Uint64 activeBaseGeneration = 0;

  void DiscoverAbcGroups(const char *basePath, Uint64 startTime, Uint64 endTime,
                         Uint64 ownerBaseGeneration);
  void DrawAbcForBase(Kotonoha_Picture *picture, SDL_Renderer *renderer,
                      Uint64 atMs);
  void RenderTexture(SDL_Renderer *render, SDL_Texture *target,
                     Kotonoha_Picture *picture, bool &cleaned);

public:
  void Reset();

  Image(Kotonoha_time *time);
  Image(Kotonoha_time *time, bool ignoreMissingAssets);

  Uint64 Register(const char *path, Uint64 startTime, Uint64 endTime, Uint8 id);
  bool ActivateBase(Uint64 baseGeneration);
  Uint64 ActiveBaseGeneration() const;
  bool IsActiveGroup(const AbcGroup *group, Uint64 atMs);
  AbcGroup *FindAbcGroup(const std::string &key, Uint64 atMs);
  bool BindAbcGroup(AbcGroup *group, Uint64 ownerBaseGeneration,
                    Uint64 voiceStartMs, Uint64 voiceEndMs);
  void UnbindAbcGroup(AbcGroup *group);
  void SelectAbcState(AbcGroup *group, Uint8 index);
  std::vector<MediaBufferInfo> GetBufferInfo() const;

  static Kotonoha_Scene_Status RenderCallback(KOTONOHA_SCENE_CALL);

  /*
   * Keep both callback shapes used across the merged codebase: Canvas consumes
   * the current four-argument callback, while the frozen G2.7 tests still call
   * the historical five-argument form that carried an event queue. Image never
   * consumed that queue, so both forms intentionally reach the same renderer.
   */
  struct RenderDispatcher {
    using Callback = Kotonoha_Scene_Status (*)(SDL_Window *, SDL_Renderer *,
                                               void *, SDL_Texture *);

    operator Callback() const { return &Image::RenderCallback; }

    Kotonoha_Scene_Status operator()(SDL_Window *window, SDL_Renderer *render,
                                     void *userData, SDL_Texture *target) const {
      return Image::RenderCallback(window, render, userData, target);
    }

    Kotonoha_Scene_Status operator()(SDL_Window *window, SDL_Renderer *render,
                                     void *legacyEventQueue, void *userData,
                                     SDL_Texture *target) const {
      (void)legacyEventQueue;
      return Image::RenderCallback(window, render, userData, target);
    }
  };

  inline static const RenderDispatcher Render{};

  ~Image();
};
} // namespace Kotonoha
