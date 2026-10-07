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
    std::string basePath;
    Uint64 startTime = 0, endTime = 0;
    SDL_Texture* textures[3] = {nullptr, nullptr, nullptr};
    Uint8 activeIndex = 0;
  };
private:
  Kotonoha_time *timeManager = nullptr;
  std::vector<Kotonoha_Picture *> pictures = std::vector<Kotonoha_Picture *>();
  SDL_Mutex *lock = nullptr;
  std::vector<std::unique_ptr<AbcGroup>> abcGroups;
  void DiscoverAbcGroups(const char* basePath, Uint64 startTime, Uint64 endTime);
  void DrawAbcForBase(Kotonoha_Picture* picture, SDL_Renderer* renderer);

public:
  void Reset();

  Image(Kotonoha_time *time);

  void Register(const char *path, Uint64 startTime, Uint64 endTime, Uint8 id);
  AbcGroup* FindAbcGroup(const std::string& key, Uint64 atMs);
  void SelectAbcState(AbcGroup* group, Uint8 index);

  static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  ~Image();
};
} // namespace Kotonoha
