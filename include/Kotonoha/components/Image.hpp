#pragma once
#include <vector>
#include <memory>
#include <string>
#include <Kotonoha/SchoolDaysAbcRuntime.hpp>

extern "C" {
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/ImageRender.h>
#include <Kotonoha/utils/Time.h>
#include <Kotonoha/utils/UserEvents.h>
}

namespace Kotonoha {
class Image {
public:
  struct AbcGroup {
    SchoolDaysAbcGroupResources resources;
    Uint64 ownerBaseGeneration = 0;
    Uint64 startTime = 0, endTime = 0;
    SDL_Texture* textures[3] = {nullptr, nullptr, nullptr};
    Uint8 activeIndex = 0;
    bool bound = false;
    Uint64 bindingStartTime = 0, bindingEndTime = 0;
  };
private:
  Kotonoha_time *timeManager = nullptr;
  std::vector<Kotonoha_Picture *> pictures = std::vector<Kotonoha_Picture *>();
  SDL_Mutex *lock = nullptr;
  std::vector<std::unique_ptr<AbcGroup>> abcGroups;
  Uint64 nextBaseGeneration = 1;
  Uint64 activeBaseGeneration = 0;
  void DiscoverAbcGroups(const char* basePath, Uint64 startTime, Uint64 endTime,
                         Uint64 ownerBaseGeneration);
  void DrawAbcForBase(Kotonoha_Picture* picture, SDL_Renderer* renderer,
                      Uint64 atMs);

public:
  void Reset();

  Image(Kotonoha_time *time);

  Uint64 Register(const char *path, Uint64 startTime, Uint64 endTime, Uint8 id);
  bool ActivateBase(Uint64 baseGeneration);
  Uint64 ActiveBaseGeneration() const;
  bool IsActiveGroup(const AbcGroup* group, Uint64 atMs);
  AbcGroup* FindAbcGroup(const std::string& key, Uint64 atMs);
  bool BindAbcGroup(AbcGroup* group, Uint64 ownerBaseGeneration,
                    Uint64 voiceStartMs, Uint64 voiceEndMs);
  void UnbindAbcGroup(AbcGroup* group);
  void SelectAbcState(AbcGroup* group, Uint8 index);

  static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  ~Image();
};
} // namespace Kotonoha
