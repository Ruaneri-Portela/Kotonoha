#pragma once

#include <Kotonoha/SceneTime.h>
#include <Kotonoha/components/Prompt.hpp>

#include <cstddef>
#include <cstdlib>
#include <string>

namespace Kotonoha {

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

// Authenticated School Days HQ v1.02 choice interaction. This is deliberately
// game-owned: Kotonoha only hosts PromptBehavior and knows nothing about these
// retail hit regions, colors, timeout or AUTO semantics.
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
  std::size_t ChoiceCount() const { return choiceCount_; }
  bool Seeded() const { return seeded_; }

  bool ChoiceCenter(int index, float *x, float *y) const {
    if (index < 0 || static_cast<std::size_t>(index) >= choiceCount_ ||
        x == nullptr || y == nullptr) {
      return false;
    }
    *x = 0.5f;
    *y = 0.5f;
    if (choiceCount_ == 2) {
      const float c = (static_cast<float>(index) + 0.5f) / 2.0f;
      if (policy_.twoChoiceOrientation ==
          SchoolDaysPromptOrientation::LeftRight) {
        *x = c;
      } else if (policy_.twoChoiceOrientation ==
                 SchoolDaysPromptOrientation::TopBottom) {
        *y = c;
      } else {
        return false;
      }
    }
    return true;
  }

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
    if (choiceCount_ == 1) return 0;
    if (choiceCount_ != 2) return -1;
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
    if (!InputOpenAtTick(tick)) return;
    storedSelection_ = HitNormalized(x, y);
    seeded_ = true;
  }

  void PointerMotion(float x, float y, Kotonoha_SceneTick tick) {
    if (!InputOpenAtTick(tick)) return;
    storedSelection_ = HitNormalized(x, y);
    seeded_ = true;
  }

  bool LeftClick(float x, float y, Kotonoha_SceneTick tick, int *result) const {
    if (!InputOpenAtTick(tick) || result == nullptr || *result != -2) {
      return false;
    }
    const int hit = HitNormalized(x, y);
    if (hit < 0 || hit != storedSelection_) return false;
    *result = hit;
    return true;
  }

  void ResetSelection() {
    storedSelection_ = -1;
    seeded_ = false;
  }

  static bool AcceptsEventType(Uint32 type) {
    return type == SDL_EVENT_MOUSE_MOTION ||
           type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  }

private:
  int ResolveTimeoutResult() const {
    if (!autoMode_ || (choiceCount_ != 1 && choiceCount_ != 2)) return -1;
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

class SchoolDaysPromptBehavior final : public PromptBehavior {
public:
  explicit SchoolDaysPromptBehavior(const SchoolDaysPromptPolicy &policy);
  ~SchoolDaysPromptBehavior() override;

  bool Prepare(Prompt &prompt, Kotonoha_Game *gameContext,
               std::string *error) override;
  void Update(Prompt &prompt) override;
  bool IsActive(const Prompt &prompt) const override;
  bool SelectChoice(Prompt &prompt, int index, Audio *audioManager) override;
  void HandleEvent(Prompt &prompt, SDL_Event *event, Kotonoha_Game *gameContext,
                   Audio *audioManager) override;
  enum Kotonoha_Scene_Status Render(Prompt &prompt, SDL_Window *window,
                                    SDL_Renderer *renderer,
                                    SDL_Texture *target) override;
  void Reset(Prompt &prompt) override;
  void SetAutoMode(bool enabled) override { interaction_.SetAutoMode(enabled); }
  void PlayPendingAudio(Prompt &prompt, Audio *audioManager) override;
  bool PresentationTailActive(const Prompt &prompt) const override;
  bool PresentationNeedsFinalFrame(const Prompt &prompt) const override;
  bool PresentationFinalFrameRendered(const Prompt &prompt) const override {
    (void)prompt;
    return interaction_.Enabled() && exitActive_ && exitFinalFrameRendered_;
  }

  SchoolDaysPromptInteraction &Interaction() { return interaction_; }
  const SchoolDaysPromptInteraction &Interaction() const { return interaction_; }

private:
  enum class PendingSound { None, Select, Cancel };

  SchoolDaysPromptPolicy policy_{};
  SchoolDaysPromptInteraction interaction_{};
  SDL_Texture *scratchTexture_ = nullptr;
  std::string assetRoot_{};
  std::string selectAudioPath_{};
  std::string cancelAudioPath_{};
  bool presentationConfigured_ = false;
  bool presentationAttempted_ = false;
  bool interactionConfigured_ = false;
  bool exitActive_ = false;
  bool exitFinalFrameRendered_ = false;
  Kotonoha_SceneTick exitStartTick_ = 0;
  int exitSelected_ = -1;
  PendingSound pendingSound_ = PendingSound::None;

  static Kotonoha_SceneTick CurrentTick(const Prompt &prompt);
  static bool PointFromWindow(SDL_Renderer *renderer, float windowX,
                              float windowY, float *normalizedX,
                              float *normalizedY);
  void ClearScratchTexture();
  void ArmExit(const Prompt &prompt, int selected);
  bool RenderRetail(Prompt &prompt, SDL_Renderer *renderer, int outputWidth,
                    int outputHeight);
};

} // namespace Kotonoha
