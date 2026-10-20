#pragma once

#include <Kotonoha/components/Upscaling.hpp>
#include <SDL3/SDL_render.h>
#include <memory>
#include <string>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/utils/Time.h>

namespace Kotonoha {

struct AbcGroup {
  std::string key;
  std::string paths[3];

  Uint64 ownerBaseGeneration = 0;
  Uint64 startTime = 0;
  Uint64 endTime = 0;

  Uint64 bindingStartTime = 0;
  Uint64 bindingEndTime = 0;

  SDL_Texture *textures[3] = {nullptr, nullptr, nullptr};
  Kotonoha_ImageLoad loads[3] = {};

  Uint8 activeIndex = 0;
  bool bound = false;

  ~AbcGroup();
};

class Image : public Upscaling {
private:
  Kotonoha_Time *timeManager = nullptr;
  bool ignoreMissingAssets = false;

  std::vector<Kotonoha_Picture *> pictures;
  SDL_Mutex *lock = nullptr;

  std::vector<std::unique_ptr<AbcGroup>> abcGroups;
  Uint64 nextBaseGeneration = 1;

  void DiscoverAbcGroups(const char *basePath, Uint64 startTime, Uint64 endTime,
                         Uint64 ownerBaseGeneration);

  void DrawAbcForBase(Kotonoha_Picture *picture, SDL_Renderer *renderer,
                      Uint64 atMs);

  void RemoveAbcGroupsForBase(Uint64 baseGeneration);

  void RenderTexture(SDL_Renderer *render, SDL_Texture *target,
                     Kotonoha_Picture *picture, bool &cleaned);

public:
  Image(Kotonoha_Time *time, bool ignoreMissingAssets);
  ~Image();

  void Reset();

  Uint64 Register(const char *path, Uint64 startTime, Uint64 endTime, Uint8 id);

  void SetAbcState(const std::string &key, Uint8 index, Uint64 voiceStartMs,
                   Uint64 voiceEndMs);

  static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);
};

} // namespace Kotonoha
