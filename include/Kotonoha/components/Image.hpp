#pragma once
#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/components/MediaBufferInfo.hpp>
#include <Kotonoha/components/Upscaling.hpp>
#include <Kotonoha/graphics/AbcAnimation.hpp>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/utils/Time.h>
}

namespace Kotonoha {
class Image : public Upscaling {
public:
  struct AbcGroup {
    AbcGroupResources resources;
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

  using RenderDispatcher = ComponentRenderDispatcher<Image, &Image::RenderCallback>;
  inline static const RenderDispatcher Render{};

  ~Image();
};
} // namespace Kotonoha
