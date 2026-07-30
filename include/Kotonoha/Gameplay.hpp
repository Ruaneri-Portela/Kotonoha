#pragma once
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Canvas.hpp>
#include <Kotonoha/components/Events.hpp>
#include <Kotonoha/components/Image.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/components/Sound.hpp>
#include <Kotonoha/components/Video.hpp>

extern "C" {
#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/renders/AudioRender.h>
#include <Kotonoha/renders/TextRender.h>
#include <Kotonoha/utils/Time.h>
}

namespace Kotonoha {
class Gameplay {
private:
  Canvas *drawCanvas = nullptr;
  Event *eventManager = nullptr;
  float aspectRatio = 16.0f / 9.0f;
  int windowWidth = 0, windowHeight = 0;
  bool lastPauseStatus = false, playOnlyOnFocus = false, firstFocus = true;

  void UpdateCanvasSize(SDL_Window *window, SDL_Renderer *renderer);

public:
  struct Kotonoha_textData *sb = nullptr;
  struct Kotonoha_time *tm = nullptr;
  Video *video = nullptr;
  Image *image = nullptr;
  Audio *audio = nullptr;
  Prompt *prompt = nullptr;
  int promptId = -1;
  bool putPrompt = false;
  bool reset = false;

  std::string script;

  Gameplay(const char *scriptPath, struct Kotonoha_Game *gameContext);
  SDL_AppResult Main(struct Kotonoha_Game *gameContext);

  void Pause();
  void Resume();
  void TogglePause();
  void Reset();
  void SeekForward(Uint64 ms);
  void SeekBackward(Uint64 ms);
  float GetTime();
  float GetLastTime();
  void SetTime(float time);

  ~Gameplay();
};
} // namespace Kotonoha