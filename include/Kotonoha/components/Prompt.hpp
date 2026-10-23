#pragma once
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <Kotonoha/Kotonoha.h>
#include <Kotonoha/SceneTime.h>
#include <Kotonoha/routing/SchoolDaysFontData.hpp>
#include <Kotonoha/utils/Time.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace Kotonoha {

class Audio;

enum class SchoolDaysPromptOrientation {
  Unconfigured,
  LeftRight,
  TopBottom,
};

struct SchoolDaysPromptPolicy {
  bool enabled = false;
  SchoolDaysPromptOrientation twoChoiceOrientation =
      SchoolDaysPromptOrientation::Unconfigured;
  Kotonoha_SceneTick startTick = 0;
  Kotonoha_SceneTick endTick = 0;
};

/*
 * Authenticated School Days HQ v1.02 choice interaction.  The shipped CMAPs
 * cover the full logical canvas for one choice and split it exactly in half
 * for two choices.  Shipped v1.02 has UseEnglish=1 + SelectType=1, which the
 * authenticated selector maps to the *_H top/bottom profile.  Unconfigured
 * remains available only for profiles whose orientation has not been proved.
 *
 * Interaction state remains isolated from presentation; the Prompt's School
 * Days branch separately owns the authenticated FONTDATA/SE/fade renderer.
 */
class SchoolDaysPromptInteraction {
public:
  using AutoChoiceRandomSource = Uint32 (*)(void *userdata);

  static constexpr Uint32 NormalArgb = 0xFFF0F0F0u;
  static constexpr Uint32 SelectedArgb = 0xFFFE4A1Fu;

  void Configure(const SchoolDaysPromptPolicy &policy, std::size_t choiceCount) {
    policy_ = policy;
    choiceCount_ = choiceCount;
    storedSelection_ = -1;
    seeded_ = false;
  }

  bool Enabled() const { return policy_.enabled; }
  bool AutoMode() const { return autoMode_; }
  SchoolDaysPromptOrientation Orientation() const {
    return policy_.twoChoiceOrientation;
  }
  int StoredSelection() const { return storedSelection_; }
  bool Seeded() const { return seeded_; }

  void SetAutoMode(bool enabled) { autoMode_ = enabled; }
  void SetAutoChoiceRandomSource(AutoChoiceRandomSource source,
                                 void *userdata = nullptr) {
    autoChoiceRandomSource_ = source;
    autoChoiceRandomUserdata_ = userdata;
  }

  bool InputOpenAtTick(Kotonoha_SceneTick tick) const {
    if (!policy_.enabled || tick < policy_.startTick ||
        policy_.endTick <= policy_.startTick || tick >= policy_.endTick) {
      return false;
    }
    return (policy_.endTick - tick) > 1;
  }

  bool VisibleAtTick(Kotonoha_SceneTick tick) const {
    return policy_.enabled && policy_.endTick > policy_.startTick &&
           tick >= policy_.startTick && tick < policy_.endTick;
  }

  bool AdvanceToTick(Kotonoha_SceneTick tick, int *result) const {
    if (!policy_.enabled || result == nullptr || *result != -2 ||
        tick < policy_.startTick || policy_.endTick <= policy_.startTick) {
      return false;
    }
    if (tick >= policy_.endTick || (policy_.endTick - tick) <= 1) {
      *result = ResolveTimeoutResult();
      return true;
    }
    return false;
  }

  int HitNormalized(float x, float y) const {
    if (!policy_.enabled || x < 0.0f || x >= 1.0f || y < 0.0f ||
        y >= 1.0f) {
      return -1;
    }
    if (choiceCount_ == 1) {
      return 0;
    }
    if (choiceCount_ != 2) {
      return -1;
    }
    if (policy_.twoChoiceOrientation ==
        SchoolDaysPromptOrientation::LeftRight) {
      return x < 0.5f ? 0 : 1;
    }
    if (policy_.twoChoiceOrientation ==
        SchoolDaysPromptOrientation::TopBottom) {
      return y < 0.5f ? 0 : 1;
    }
    return -1;
  }

  void SeedPointer(float x, float y, Kotonoha_SceneTick tick) {
    if (!InputOpenAtTick(tick)) {
      return;
    }
    storedSelection_ = HitNormalized(x, y);
    seeded_ = true;
  }

  void PointerMotion(float x, float y, Kotonoha_SceneTick tick) {
    if (!InputOpenAtTick(tick)) {
      return;
    }
    storedSelection_ = HitNormalized(x, y);
    seeded_ = true;
  }

  bool LeftClick(float x, float y, Kotonoha_SceneTick tick, int *result) const {
    if (!InputOpenAtTick(tick) || result == nullptr || *result != -2) {
      return false;
    }
    const int hit = HitNormalized(x, y);
    if (hit < 0 || hit != storedSelection_) {
      return false;
    }
    *result = hit;
    return true;
  }

  void ResetSelection() {
    storedSelection_ = -1;
    seeded_ = false;
  }

  static bool AcceptsEventType(Uint32 type) {
    return type == SDL_EVENT_MOUSE_MOTION || type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  }

private:
  int ResolveTimeoutResult() const {
    if (!autoMode_ || (choiceCount_ != 1 && choiceCount_ != 2)) {
      return -1;
    }

    Uint32 randomValue = 0;
    if (autoChoiceRandomSource_ != nullptr) {
      randomValue = autoChoiceRandomSource_(autoChoiceRandomUserdata_);
    } else {
      std::srand(static_cast<unsigned int>(SDL_GetTicks()));
      randomValue = static_cast<Uint32>(std::rand());
    }
    const Uint32 span = static_cast<Uint32>(choiceCount_);
    return static_cast<int>(randomValue % (span + 1u)) - 1;
  }

  SchoolDaysPromptPolicy policy_{};
  std::size_t choiceCount_ = 0;
  int storedSelection_ = -1;
  bool seeded_ = false;
  bool autoMode_ = false;
  AutoChoiceRandomSource autoChoiceRandomSource_ = nullptr;
  void *autoChoiceRandomUserdata_ = nullptr;
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
  SchoolDaysPromptInteraction schoolDaysInteraction{};
  std::shared_ptr<SchoolDaysFontData> schoolDaysFontData{};
  SDL_Texture *schoolDaysScratchTexture = nullptr;
  std::string schoolDaysAssetRoot{};
  std::string schoolDaysSelectAudioPath{};
  std::string schoolDaysCancelAudioPath{};
  bool schoolDaysPresentationConfigured = false;
  bool schoolDaysPresentationAttempted = false;
  std::string schoolDaysPresentationError{};
  bool schoolDaysExitActive = false;
  bool schoolDaysExitFinalFrameRendered = false;
  Kotonoha_SceneTick schoolDaysExitStartTick = 0;
  int schoolDaysExitSelected = -1;
  enum class SchoolDaysPendingSound { None, Select, Cancel };
  SchoolDaysPendingSound schoolDaysPendingSound = SchoolDaysPendingSound::None;

  void ClearLayoutTextures();
  void ClearSchoolDaysScratchTexture();
  void ArmSchoolDaysExit(int selected);
  bool RenderSchoolDays(SDL_Renderer *renderer, int outputWidth,
                        int outputHeight);
  bool SchoolDaysPointFromWindow(SDL_Renderer *renderer, float windowX,
                                 float windowY, float *normalizedX,
                                 float *normalizedY) const;
  Kotonoha_SceneTick SchoolDaysCurrentTick() const;

public:
  Prompt(std::vector<std::string> prompt, int *promptResult, Uint64 start,
         Uint64 end, Kotonoha_Time *time, TTF_Font *font,
         const char *clickAudioPath = nullptr);

  void Update();
  void SetViewport(SDL_FRect renderViewport) { viewport = renderViewport; }
  bool IsActive() const;
  void HandleEvent(SDL_Event *event, Kotonoha_Game *gameContext,
                   Audio *audioManager);
  void EnableSchoolDaysPolicy(const SchoolDaysPromptPolicy &policy);
  void SetSchoolDaysAutoMode(bool enabled) {
    schoolDaysInteraction.SetAutoMode(enabled);
  }
  void SetSchoolDaysAutoChoiceRandomSource(
      SchoolDaysPromptInteraction::AutoChoiceRandomSource source,
      void *userdata = nullptr) {
    schoolDaysInteraction.SetAutoChoiceRandomSource(source, userdata);
  }
  bool ConfigureSchoolDaysPresentation(const char *assetRoot,
                                       std::string *error = nullptr);
  void PlaySchoolDaysPendingAudio(Audio *audioManager);
  bool SchoolDaysPresentationTailActive() const;
  bool SchoolDaysPresentationNeedsFinalFrame() const;
  bool SchoolDaysPresentationFinalFrameRendered() const {
    return schoolDaysInteraction.Enabled() && schoolDaysExitActive &&
           schoolDaysExitFinalFrameRendered;
  }
  bool UsesSchoolDaysPolicy() const { return schoolDaysInteraction.Enabled(); }
  int SchoolDaysStoredSelection() const {
    return schoolDaysInteraction.StoredSelection();
  }

  static enum Kotonoha_Scene_Status Render(KOTONOHA_SCENE_CALL);

  void Reset();
  int Result() const { return promptResult != nullptr ? *promptResult : -2; }

  ~Prompt();
};

} // namespace Kotonoha
