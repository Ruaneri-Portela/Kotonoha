#pragma once
#include <memory>
#include <string>
#include <vector>

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
    std::string key;
    std::string paths[3];
    Uint64 ownerBaseGeneration = 0;
    Uint64 startTime = 0;
    Uint64 endTime = 0;
    Uint64 bindingStartTime = 0;
    Uint64 bindingEndTime = 0;
    SDL_Texture* textures[3] = { nullptr, nullptr, nullptr };
    Uint8 activeIndex = 0;
    bool bound = false;
  };

private:
  Kotonoha_time *timeManager = nullptr;
  std::vector<Kotonoha_Picture *> pictures = std::vector<Kotonoha_Picture *>();
  SDL_Mutex *lock = nullptr;
  std::vector<std::unique_ptr<AbcGroup>> abcGroups;
  Uint64 nextBaseGeneration = 1;

  void DiscoverAbcGroups(const char* basePath, Uint64 startTime,
                         Uint64 endTime, Uint64 ownerBaseGeneration);
  void DrawAbcForBase(Kotonoha_Picture* picture, SDL_Renderer* renderer,
                      Uint64 atMs);

public:
  void Reset();

  Image(Kotonoha_time *time);

  Uint64 Register(const char *path, Uint64 startTime, Uint64 endTime, Uint8 id);
  void SetAbcState(const std::string& key, Uint8 index,
                   Uint64 voiceStartMs, Uint64 voiceEndMs);

  static Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  ~Image();
};
} // namespace Kotonoha