#pragma once
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/SceneTime.h>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace Kotonoha {

class Audio;
class Prompt;

// Optional game-owned behavior plugged into the generic Prompt host. The
// engine owns input/layout for its default prompt; a game profile can replace
// those semantics without putting game policy in Kotonoha.
class PromptBehavior {
public:
  virtual ~PromptBehavior() = default;
  virtual bool Prepare(Prompt &, Kotonoha_Game *, std::string *) { return true; }
  virtual void Update(Prompt &) = 0;
  virtual bool IsActive(const Prompt &) const = 0;
  virtual bool SelectChoice(Prompt &, int, Audio *) = 0;
  virtual void HandleEvent(Prompt &, SDL_Event *, Kotonoha_Game *, Audio *) = 0;
  virtual enum Kotonoha_Scene_Status Render(Prompt &, SDL_Window *,
                                            SDL_Renderer *, SDL_Texture *) = 0;
  virtual void Reset(Prompt &) = 0;
  virtual void SetAutoMode(bool) {}
  virtual void PlayPendingAudio(Prompt &, Audio *) {}
  virtual bool PresentationTailActive(const Prompt &) const { return false; }
  virtual bool PresentationNeedsFinalFrame(const Prompt &) const { return false; }
  virtual bool PresentationFinalFrameRendered(const Prompt &) const {
    return false;
  }
};

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
  SDL_FRect viewport = {0, 0, 0, 0};
  std::vector<PromptLayoutItem> lastLayout{};
  bool verticalList = false;
  int keyIndex = -1; // keyboard-highlighted entry of lastLayout
  std::unique_ptr<PromptBehavior> behavior{};

  void ClearLayoutTextures();

public:
  Prompt(std::vector<std::string> prompt, int *promptResult, Uint64 start,
         Uint64 end, Kotonoha_Time *time, TTF_Font *font,
         const char *clickAudioPath = nullptr);

  void Update();
  // Lay out even one or two choices as a vertical list (menus).
  void SetVerticalList(bool vertical) { verticalList = vertical; }
  void SetViewport(SDL_FRect renderViewport) { viewport = renderViewport; }
  bool IsActive() const;
  const std::vector<std::string> &Choices() const { return prompt; }
  // Answers the pending choice as if the player pointed at and clicked it
  // (any choice count for the generic layout). False when the prompt is not
  // accepting input.
  bool SelectChoice(int index, Audio *audioManager);
  void HandleEvent(SDL_Event *event, Kotonoha_Game *gameContext,
                   Audio *audioManager);
  void SetBehavior(std::unique_ptr<PromptBehavior> value);
  PromptBehavior *Behavior() { return behavior.get(); }
  const PromptBehavior *Behavior() const { return behavior.get(); }
  bool PrepareBehavior(Kotonoha_Game *gameContext,
                       std::string *error = nullptr);
  void SetAutoMode(bool enabled) {
    if (behavior != nullptr) behavior->SetAutoMode(enabled);
  }
  void PlayPendingAudio(Audio *audioManager) {
    if (behavior != nullptr) behavior->PlayPendingAudio(*this, audioManager);
  }
  bool PresentationTailActive() const {
    return behavior != nullptr && behavior->PresentationTailActive(*this);
  }
  bool PresentationNeedsFinalFrame() const {
    return behavior != nullptr && behavior->PresentationNeedsFinalFrame(*this);
  }
  bool PresentationFinalFrameRendered() const {
    return behavior != nullptr &&
           behavior->PresentationFinalFrameRendered(*this);
  }
  // Start (ms) of a choice that has not been answered yet; seeks must not
  // jump over it.
  bool PendingStartMs(Uint64 *startMs) const {
    if (promptResult == nullptr || *promptResult != -2) return false;
    if (startMs != nullptr) *startMs = startTime;
    return true;
  }
  bool UsesCustomBehavior() const { return behavior != nullptr; }

  // Narrow extension surface for game-owned PromptBehavior implementations.
  Kotonoha_Time *Time() const { return time; }
  TTF_Font *Font() const { return font; }
  Uint64 StartTimeMs() const { return startTime; }
  Uint64 EndTimeMs() const { return endTime; }
  int *ResultPointer() const { return promptResult; }
  SDL_FRect Viewport() const { return viewport; }

  static enum Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  void Reset();
  int Result() const { return promptResult != nullptr ? *promptResult : -2; }

  ~Prompt();
};

} // namespace Kotonoha
