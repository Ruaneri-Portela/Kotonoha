#pragma once
#include <string>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace Kotonoha {

class Audio;

struct PromptLayoutItem {
  int originalIndex;
  SDL_FRect rect;
  SDL_Texture *textTexture = nullptr;
  SDL_Texture *shadowTexture = nullptr;
  int textW = 0;
  int textH = 0;
  int shadowW = 0;
  int shadowH = 0;
};

class Prompt {
private:
  std::vector<std::string> prompt{};
  int *promptResult = nullptr;
  Kotonoha_Time *time = nullptr;
  Uint64 startTime = 0;
  Uint64 endTime = 0;
  TTF_Font *font = nullptr;
  std::string audioPath{};
  std::vector<PromptLayoutItem> lastLayout{};

  void ClearLayoutTextures();

public:
  Prompt(std::vector<std::string> prompt, int *promptResult, Uint64 start,
         Uint64 end, Kotonoha_Time *time, TTF_Font *font,
         const char *clickAudioPath = nullptr);

  void HandleEvent(SDL_Event *event, Kotonoha_Game *gameContext,
                   Audio *audioManager);

  static enum Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);
  ~Prompt();
};

} // namespace Kotonoha