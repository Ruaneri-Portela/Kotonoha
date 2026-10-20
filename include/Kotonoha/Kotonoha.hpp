#pragma once
#include <Kotonoha/Gameplay.hpp>
#include <SDL3_ttf/SDL_ttf.h>

#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/FpsRender.h>
#include <Kotonoha/renders/LoadingRender.h>
#include <Kotonoha/renders/TimestampRender.h>
#include <Kotonoha/parsers/Config.h>
#include <Kotonoha/utils/Path.h>

#include <atomic>
#include <string>
#include <tuple>
#include <vector>

namespace Kotonoha {
class Kotonoha {
private:
  Kotonoha_Game &gameContext;

  SDL_Texture *sceneTransitionFrame = nullptr;
  SDL_Thread *processPool = nullptr;
  std::atomic_bool processPoolRunning{true};
  std::vector<std::tuple<SDL_ThreadFunction, void *>> processPoolTasks;

  std::string preferredUpscaler = "Auto";
  bool sceneTransitionPrepared = false;
  bool sceneTransitionActive = false;

  Uint64 lastMouseTime = 0;
  Uint64 lastSceneFrameCaptureTime = 0;
  size_t lastScene = static_cast<size_t>(-1);

  bool ParseArguments(int argc, char *argv[]);
  void LoadSubtitleStylesFile(const char *path);
  bool CaptureSceneTransitionFrame(bool activateTransition = true);

  static int SDLCALL EventsThread(void *data);

public:
  std::unordered_map<std::string, TTF_Font *> fonts;
  std::vector<Gameplay *> gameplays;

  Sound::Channel *BGM = nullptr;
  Sound::Channel *Voice = nullptr;
  Sound::Channel *Se = nullptr;

  bool LoadScriptFile(const char *path);
  void DeleteGameplay(Gameplay *gameplay);
  void CacheSceneTransitionFrameIfNeeded(Gameplay *current);
  void PresentSceneTransitionFrame(Gameplay *incoming);

  void ClearGameplays();
  Kotonoha(SDL_AppResult *initStatus, struct Kotonoha_Game &gameContext);

  SDL_AppResult Event(SDL_Event *event);

  SDL_AppResult Main(Gameplay **out);

  ~Kotonoha();
};
} // namespace Kotonoha
