#include <Kotonoha/routing/SchoolDaysPromptBehavior.hpp>

#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace Kotonoha {
namespace {

std::vector<int> VisibleChoiceIndices(const Prompt &prompt) {
  std::vector<int> indices;
  const auto &choices = prompt.Choices();
  indices.reserve(choices.size());
  for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
    if (!choices[static_cast<std::size_t>(i)].empty()) indices.push_back(i);
  }
  return indices;
}

std::u16string Utf8ToUtf16(const std::string &text) {
  if (text.empty()) return {};
  char *converted = SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(),
                                    text.size());
  if (converted == nullptr) return {};
  const auto *units = reinterpret_cast<const char16_t *>(converted);
  std::u16string result;
  while (*units != 0) result.push_back(*units++);
  SDL_free(converted);
  return result;
}

std::string Utf16ToUtf8(const std::u16string &text) {
  if (text.empty()) return {};
  char *converted = SDL_iconv_string(
      "UTF-8", "UTF-16LE", reinterpret_cast<const char *>(text.c_str()),
      text.size() * sizeof(char16_t));
  if (converted == nullptr) return {};
  std::string result(converted);
  SDL_free(converted);
  return result;
}

} // namespace

SchoolDaysPromptBehavior::SchoolDaysPromptBehavior(
    const SchoolDaysPromptPolicy &policy)
    : policy_(policy) {}

SchoolDaysPromptBehavior::~SchoolDaysPromptBehavior() { ClearScratchTexture(); }

Kotonoha_SceneTick
SchoolDaysPromptBehavior::CurrentTick(const Prompt &prompt) {
  return prompt.Time() != nullptr
             ? Kotonoha_MillisecondsToSceneTick(KotonohaTimeGet(prompt.Time()))
             : 0;
}

bool SchoolDaysPromptBehavior::PointFromWindow(SDL_Renderer *renderer,
                                               float windowX, float windowY,
                                               float *normalizedX,
                                               float *normalizedY) {
  if (renderer == nullptr || normalizedX == nullptr || normalizedY == nullptr)
    return false;
  float renderX = 0.0f;
  float renderY = 0.0f;
  if (!SDL_RenderCoordinatesFromWindow(renderer, windowX, windowY, &renderX,
                                       &renderY)) {
    return false;
  }
  int outputWidth = 0;
  int outputHeight = 0;
  if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight) ||
      outputWidth <= 0 || outputHeight <= 0) {
    return false;
  }
  *normalizedX = renderX / static_cast<float>(outputWidth);
  *normalizedY = renderY / static_cast<float>(outputHeight);
  return true;
}

void SchoolDaysPromptBehavior::ClearScratchTexture() {
  if (scratchTexture_ != nullptr) {
    SDL_DestroyTexture(scratchTexture_);
    scratchTexture_ = nullptr;
  }
}

bool SchoolDaysPromptBehavior::Prepare(Prompt &prompt,
                                       Kotonoha_Game *gameContext,
                                       std::string *error) {
  if (error != nullptr) error->clear();
  if (!interactionConfigured_) {
    interaction_.Configure(policy_, prompt.Choices().size());
    interactionConfigured_ = true;
  }
  if (!policy_.enabled) return true;

  const auto root = SelectSchoolDaysAssetRoot(
      gameContext != nullptr ? gameContext->profileAssetsRoot : nullptr,
      gameContext != nullptr ? gameContext->config.assetsPath : nullptr);
  const std::string selectedRoot = root.root != nullptr ? root.root : "";
  if (presentationAttempted_ && selectedRoot == assetRoot_) {
    return presentationConfigured_ && prompt.Font() != nullptr;
  }

  assetRoot_ = selectedRoot;
  presentationAttempted_ = true;
  presentationConfigured_ = false;
  selectAudioPath_.clear();
  cancelAudioPath_.clear();
  ClearScratchTexture();

  if (prompt.Font() == nullptr) {
    if (error != nullptr) *error = "Kotonoha choice font is unavailable";
    return false;
  }

  const auto select = ResolveSchoolDaysAssetPath(
      assetRoot_.c_str(), "SysSe/NewSys/SeSelect.OGG");
  if (select.present && !select.collision) selectAudioPath_ = select.physical;
  const auto cancel = ResolveSchoolDaysAssetPath(
      assetRoot_.c_str(), "SysSe/NewSys/SeCancel_01.OGG");
  if (cancel.present && !cancel.collision) cancelAudioPath_ = cancel.physical;

  presentationConfigured_ = true;
  return true;
}

void SchoolDaysPromptBehavior::ArmExit(const Prompt &prompt, int selected) {
  if (!interaction_.Enabled() || exitActive_) return;
  exitActive_ = true;
  exitFinalFrameRendered_ = false;
  exitStartTick_ = CurrentTick(prompt);
  exitSelected_ = selected;
  pendingSound_ = selected >= 0 ? PendingSound::Select : PendingSound::Cancel;
}

void SchoolDaysPromptBehavior::Update(Prompt &prompt) {
  if (!interactionConfigured_) {
    interaction_.Configure(policy_, prompt.Choices().size());
    interactionConfigured_ = true;
  }
  int *result = prompt.ResultPointer();
  const int before = result != nullptr ? *result : -2;
  interaction_.AdvanceToTick(CurrentTick(prompt), result);
  if (before == -2 && result != nullptr && *result != -2) ArmExit(prompt, *result);
}

bool SchoolDaysPromptBehavior::IsActive(const Prompt &prompt) const {
  const int *result = prompt.ResultPointer();
  return result != nullptr && *result == -2 &&
         interaction_.InputOpenAtTick(CurrentTick(prompt));
}

bool SchoolDaysPromptBehavior::SelectChoice(Prompt &prompt, int index,
                                            Audio *audioManager) {
  int *result = prompt.ResultPointer();
  if (result == nullptr || *result != -2) return false;
  float x = 0.0f;
  float y = 0.0f;
  if (!interaction_.ChoiceCenter(index, &x, &y)) return false;
  const Kotonoha_SceneTick tick = CurrentTick(prompt);
  interaction_.PointerMotion(x, y, tick);
  if (!interaction_.LeftClick(x, y, tick, result)) return false;
  ArmExit(prompt, *result);
  PlayPendingAudio(prompt, audioManager);
  return true;
}

void SchoolDaysPromptBehavior::PlayPendingAudio(Prompt &prompt,
                                                Audio *audioManager) {
  if (audioManager == nullptr || pendingSound_ == PendingSound::None ||
      prompt.Time() == nullptr || !presentationConfigured_) {
    return;
  }
  const std::string *path = pendingSound_ == PendingSound::Select
                                ? &selectAudioPath_
                                : &cancelAudioPath_;
  if (!path->empty()) {
    audioManager->AddMedia(path->c_str(), KotonohaTimeGet(prompt.Time()),
                           UINT64_MAX, false, "Se");
  }
  pendingSound_ = PendingSound::None;
}

bool SchoolDaysPromptBehavior::PresentationTailActive(
    const Prompt &prompt) const {
  if (!interaction_.Enabled() || !exitActive_ || prompt.Time() == nullptr)
    return false;
  const Kotonoha_SceneTick now = CurrentTick(prompt);
  if (now < exitStartTick_) return true;
  return (now - exitStartTick_) <
         SchoolDaysChoiceTextPlan::ExitTailTicks(exitSelected_ >= 0);
}

bool SchoolDaysPromptBehavior::PresentationNeedsFinalFrame(
    const Prompt &prompt) const {
  if (!interaction_.Enabled() || !exitActive_ || exitFinalFrameRendered_ ||
      prompt.Time() == nullptr) {
    return false;
  }
  const Kotonoha_SceneTick now = CurrentTick(prompt);
  if (now < exitStartTick_) return false;
  return (now - exitStartTick_) >=
         SchoolDaysChoiceTextPlan::ExitTailTicks(exitSelected_ >= 0);
}

void SchoolDaysPromptBehavior::HandleEvent(Prompt &prompt, SDL_Event *event,
                                           Kotonoha_Game *gameContext,
                                           Audio *audioManager) {
  int *result = prompt.ResultPointer();
  if (event == nullptr || result == nullptr || *result != -2 ||
      !SchoolDaysPromptInteraction::AcceptsEventType(event->type)) {
    return;
  }
  Update(prompt);
  if (*result != -2 || gameContext == nullptr || gameContext->render == nullptr)
    return;

  float windowX = 0.0f;
  float windowY = 0.0f;
  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    windowX = event->motion.x;
    windowY = event->motion.y;
  } else {
    if (event->button.button != SDL_BUTTON_LEFT) return;
    windowX = event->button.x;
    windowY = event->button.y;
  }

  float normalizedX = 0.0f;
  float normalizedY = 0.0f;
  if (!PointFromWindow(gameContext->render, windowX, windowY, &normalizedX,
                       &normalizedY)) {
    return;
  }

  const Kotonoha_SceneTick tick = CurrentTick(prompt);
  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    interaction_.PointerMotion(normalizedX, normalizedY, tick);
  } else {
    interaction_.PointerMotion(normalizedX, normalizedY, tick);
    if (interaction_.LeftClick(normalizedX, normalizedY, tick, result)) {
      ArmExit(prompt, *result);
      PlayPendingAudio(prompt, audioManager);
    }
  }
}

bool SchoolDaysPromptBehavior::RenderRetail(Prompt &prompt,
                                            SDL_Renderer *renderer,
                                            int outputWidth,
                                            int outputHeight) {
  using Plan = SchoolDaysChoiceTextPlan;
  TTF_Font *font = prompt.Font();
  if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0 ||
      !presentationConfigured_ || font == nullptr) {
    return false;
  }

  const std::vector<int> visibleIndices = VisibleChoiceIndices(prompt);
  const int choiceCount =
      static_cast<int>(std::min<std::size_t>(visibleIndices.size(), 2u));
  if (choiceCount <= 0) return true;

  std::vector<std::vector<Plan::Line>> itemLines;
  itemLines.reserve(static_cast<std::size_t>(choiceCount));
  for (int slot = 0; slot < choiceCount; ++slot) {
    auto lines = Plan::WrapRetailEnglish(Utf8ToUtf16(
        prompt.Choices()[visibleIndices[static_cast<std::size_t>(slot)]]));
    if (lines.size() > 5u) return false;
    itemLines.push_back(std::move(lines));
  }

  if (scratchTexture_ == nullptr) {
    scratchTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, 1024, 512);
    if (scratchTexture_ == nullptr ||
        !SDL_SetTextureBlendMode(scratchTexture_, SDL_BLENDMODE_BLEND)) {
      ClearScratchTexture();
      return false;
    }

    SDL_Surface *scratch =
        SDL_CreateSurface(1024, 512, SDL_PIXELFORMAT_RGBA32);
    if (scratch == nullptr || !SDL_FillSurfaceRect(scratch, nullptr, 0u)) {
      if (scratch != nullptr) SDL_DestroySurface(scratch);
      ClearScratchTexture();
      return false;
    }

    bool rasterOk = true;
    for (int slot = 0; slot < choiceCount && rasterOk; ++slot) {
      const auto &lines = itemLines[static_cast<std::size_t>(slot)];
      for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const std::string utf8 = Utf16ToUtf8(lines[lineIndex].text);
        if (utf8.empty()) continue;
        SDL_Surface *textSurface = SchoolDaysSubtitleTypography::Render(
            font, utf8.c_str(), utf8.size());
        if (textSurface == nullptr) {
          rasterOk = false;
          break;
        }
        const int measuredWidth =
            std::max(1, Plan::Measure(lines[lineIndex].text));
        SDL_Rect destination{0,
                             slot * 256 + static_cast<int>(lineIndex) *
                                              Plan::kGlyphHeight,
                             std::min(measuredWidth, 1024), Plan::kGlyphHeight};
        rasterOk = SDL_BlitSurfaceScaled(textSurface, nullptr, scratch,
                                         &destination, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(textSurface);
      }
    }

    if (!rasterOk || !SDL_UpdateTexture(scratchTexture_, nullptr,
                                        scratch->pixels, scratch->pitch)) {
      SDL_DestroySurface(scratch);
      ClearScratchTexture();
      return false;
    }
    SDL_DestroySurface(scratch);
  }

  const float outputAspect =
      static_cast<float>(outputWidth) / static_cast<float>(outputHeight);
  constexpr float kAspect = 16.0f / 9.0f;
  SDL_FRect content{0.0f, 0.0f, static_cast<float>(outputWidth),
                    static_cast<float>(outputHeight)};
  if (outputAspect > kAspect) {
    content.w = content.h * kAspect;
    content.x = (static_cast<float>(outputWidth) - content.w) * 0.5f;
  } else if (outputAspect < kAspect) {
    content.h = content.w / kAspect;
    content.y = (static_cast<float>(outputHeight) - content.h) * 0.5f;
  }

  const Kotonoha_SceneTick now = CurrentTick(prompt);
  const uint32_t elapsed =
      exitActive_ && now >= exitStartTick_
          ? static_cast<uint32_t>(std::min<Kotonoha_SceneTick>(
                now - exitStartTick_, UINT32_MAX))
          : 0u;

  for (int slot = 0; slot < choiceCount; ++slot) {
    const int originalIndex = visibleIndices[static_cast<std::size_t>(slot)];
    const bool selected = exitActive_ && originalIndex == exitSelected_;
    const Uint32 argb = exitActive_
                            ? Plan::ExitArgb(selected, elapsed)
                            : interaction_.StoredSelection() == originalIndex
                                  ? Plan::kSelectedArgb
                                  : Plan::kNormalArgb;
    const Uint8 alpha = static_cast<Uint8>((argb >> 24u) & 0xffu);
    if (alpha == 0u) continue;
    SDL_SetTextureColorMod(scratchTexture_,
                           static_cast<Uint8>((argb >> 16u) & 0xffu),
                           static_cast<Uint8>((argb >> 8u) & 0xffu),
                           static_cast<Uint8>(argb & 0xffu));
    SDL_SetTextureAlphaMod(scratchTexture_, alpha);

    const auto &lines = itemLines[static_cast<std::size_t>(slot)];
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
      const auto srcPlan =
          Plan::SourceForLine(slot, static_cast<int>(lineIndex));
      const auto dstPlan = Plan::DestinationForLine(
          choiceCount, slot, static_cast<int>(lineIndex),
          static_cast<int>(lines.size()), lines[lineIndex].horizontalOffset);
      SDL_FRect src{srcPlan.x, srcPlan.y, srcPlan.w, srcPlan.h};
      SDL_FRect dst{content.x + dstPlan.x * content.w,
                    content.y + dstPlan.y * content.h,
                    dstPlan.w * content.w, dstPlan.h * content.h};
      if (!SDL_RenderTexture(renderer, scratchTexture_, &src, &dst)) return false;
    }
  }
  return true;
}

enum Kotonoha_Scene_Status
SchoolDaysPromptBehavior::Render(Prompt &prompt, SDL_Window *window,
                                 SDL_Renderer *renderer, SDL_Texture *target) {
  (void)window;
  (void)target;
  if (renderer == nullptr || prompt.Time() == nullptr ||
      prompt.ResultPointer() == nullptr) {
    return KOTONOHA_SCENE_NULL;
  }

  const Kotonoha_SceneTick now = CurrentTick(prompt);
  if (!interaction_.VisibleAtTick(now) && !exitActive_)
    return KOTONOHA_SCENE_NULL;

  bool finalExitFrame = false;
  if (exitActive_ && now >= exitStartTick_) {
    finalExitFrame = (now - exitStartTick_) >=
                     SchoolDaysChoiceTextPlan::ExitTailTicks(exitSelected_ >= 0);
  }

  if (!interaction_.Seeded() && *prompt.ResultPointer() == -2) {
    float rawMouseX = 0.0f;
    float rawMouseY = 0.0f;
    SDL_GetMouseState(&rawMouseX, &rawMouseY);
    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    if (PointFromWindow(renderer, rawMouseX, rawMouseY, &normalizedX,
                        &normalizedY)) {
      interaction_.SeedPointer(normalizedX, normalizedY, now);
    }
  }

  int outputWidth = 0;
  int outputHeight = 0;
  if (!SDL_GetCurrentRenderOutputSize(renderer, &outputWidth, &outputHeight) ||
      outputWidth <= 0 || outputHeight <= 0) {
    return KOTONOHA_SCENE_NULL;
  }

  Uint8 oldR = 0, oldG = 0, oldB = 0, oldA = 0;
  SDL_GetRenderDrawColor(renderer, &oldR, &oldG, &oldB, &oldA);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);
  if (!finalExitFrame) {
    (void)RenderRetail(prompt, renderer, outputWidth, outputHeight);
  } else {
    exitFinalFrameRendered_ = true;
  }
  SDL_SetRenderDrawColor(renderer, oldR, oldG, oldB, oldA);
  return KOTONOHA_SCENE_DRAW_OVERLAYED;
}

void SchoolDaysPromptBehavior::Reset(Prompt &prompt) {
  (void)prompt;
  interaction_.ResetSelection();
  exitActive_ = false;
  exitFinalFrameRendered_ = false;
  exitStartTick_ = 0;
  exitSelected_ = -1;
  pendingSound_ = PendingSound::None;
  ClearScratchTexture();
}

} // namespace Kotonoha
