#include <Kotonoha/components/Audio.hpp>
#include <Kotonoha/components/Prompt.hpp>
#include <Kotonoha/SchoolDaysImageAssetResolver.hpp>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>
#include <Kotonoha/utils/OverlayRender.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

namespace {

bool PromptTextValid(const std::string &text) { return !text.empty(); }

std::vector<int>
BuildVisiblePromptIndices(const std::vector<std::string> &items) {
  std::vector<int> indices;
  indices.reserve(items.size());

  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    if (PromptTextValid(items[i])) {
      indices.push_back(i);
    }
  }

  return indices;
}

std::u16string Utf8ToUtf16Prompt(const std::string &text) {
  if (text.empty()) return {};
  char *converted = SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(),
                                    text.size() + 1u);
  if (converted == nullptr) return {};
  const auto *units = reinterpret_cast<const char16_t *>(converted);
  std::u16string result;
  while (*units != 0) result.push_back(*units++);
  SDL_free(converted);
  return result;
}

void SetError(std::string *error, const std::string &value) {
  if (error != nullptr) *error = value;
}

std::shared_ptr<SchoolDaysFontData>
LoadSharedSchoolDaysChoiceFont(const std::string &assetRoot, std::string *error) {
  struct Cache {
    std::string root;
    std::shared_ptr<SchoolDaysFontData> font;
    std::string error;
    bool attempted = false;
  };
  static Cache cache;
  if (cache.attempted && cache.root == assetRoot) {
    SetError(error, cache.error);
    return cache.font;
  }

  cache = Cache{};
  cache.root = assetRoot;
  cache.attempted = true;
  const auto resolved = ResolveSchoolDaysAssetPath(
      assetRoot.c_str(), "System/System/FONTDATA_ENG.DAT");
  if (!resolved.present || resolved.collision) {
    cache.error = resolved.collision
                      ? "School Days FONTDATA_ENG path has a case collision"
                      : "School Days FONTDATA_ENG asset is missing";
    SetError(error, cache.error);
    return {};
  }

  auto font = std::make_shared<SchoolDaysFontData>();
  if (!font->LoadFile(resolved.physical, &cache.error)) {
    SetError(error, cache.error);
    return {};
  }
  cache.font = std::move(font);
  cache.error.clear();
  if (error != nullptr) error->clear();
  return cache.font;
}

} // namespace

Prompt::Prompt(std::vector<std::string> prompt, int *promptResult, Uint64 start,
               Uint64 end, Kotonoha_Time *time, TTF_Font *font,
               const char *clickAudioPath)
    : prompt(std::move(prompt)), promptResult(promptResult), time(time),
      startTime(start), endTime(end), font(font),
      audioPath(clickAudioPath ? clickAudioPath : "assets/Se/click.ogg") {}

Prompt::~Prompt() {
  ClearLayoutTextures();
  ClearSchoolDaysScratchTexture();
}

void Prompt::EnableSchoolDaysPolicy(const SchoolDaysPromptPolicy &policy) {
  schoolDaysInteraction.Configure(policy, prompt.size());
  schoolDaysExitActive = false;
  schoolDaysExitFinalFrameRendered = false;
  schoolDaysExitStartTick = 0;
  schoolDaysExitSelected = -1;
  schoolDaysPendingSound = SchoolDaysPendingSound::None;
  ClearLayoutTextures();
}

bool Prompt::ConfigureSchoolDaysPresentation(const char *assetRoot,
                                             std::string *error) {
  if (error != nullptr) error->clear();
  if (!schoolDaysInteraction.Enabled()) return true;
  const std::string root = assetRoot != nullptr ? assetRoot : "";
  if (schoolDaysPresentationAttempted && root == schoolDaysAssetRoot) {
    const bool ready = schoolDaysPresentationConfigured &&
                       schoolDaysFontData != nullptr &&
                       schoolDaysFontData->IsLoaded();
    if (error != nullptr) error->clear(); // first failed attempt already logged
    return ready;
  }

  schoolDaysAssetRoot = root;
  schoolDaysPresentationConfigured = false;
  schoolDaysPresentationAttempted = true;
  schoolDaysPresentationError.clear();
  schoolDaysFontData.reset();
  schoolDaysSelectAudioPath.clear();
  schoolDaysCancelAudioPath.clear();

  schoolDaysFontData =
      LoadSharedSchoolDaysChoiceFont(schoolDaysAssetRoot,
                                     &schoolDaysPresentationError);
  if (schoolDaysFontData == nullptr || !schoolDaysFontData->IsLoaded()) {
    SetError(error, schoolDaysPresentationError);
    return false;
  }

  const auto select = ResolveSchoolDaysAssetPath(
      schoolDaysAssetRoot.c_str(), "SysSe/NewSys/SeSelect.OGG");
  if (select.present && !select.collision) schoolDaysSelectAudioPath = select.physical;
  const auto cancel = ResolveSchoolDaysAssetPath(
      schoolDaysAssetRoot.c_str(), "SysSe/NewSys/SeCancel_01.OGG");
  if (cancel.present && !cancel.collision) schoolDaysCancelAudioPath = cancel.physical;

  schoolDaysPresentationConfigured = true;
  schoolDaysPresentationError.clear();
  return true;
}

Kotonoha_SceneTick Prompt::SchoolDaysCurrentTick() const {
  if (time == nullptr) {
    return 0;
  }
  return Kotonoha_MillisecondsToSceneTick(KotonohaTimeGet(time));
}

bool Prompt::SchoolDaysPointFromWindow(SDL_Renderer *renderer, float windowX,
                                       float windowY, float *normalizedX,
                                       float *normalizedY) const {
  if (renderer == nullptr || normalizedX == nullptr || normalizedY == nullptr) {
    return false;
  }
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

void Prompt::Update() {
  if (schoolDaysInteraction.Enabled()) {
    const int before = promptResult != nullptr ? *promptResult : -2;
    schoolDaysInteraction.AdvanceToTick(SchoolDaysCurrentTick(), promptResult);
    if (before == -2 && promptResult != nullptr && *promptResult != -2)
      ArmSchoolDaysExit(*promptResult);
    return;
  }
  if (promptResult != nullptr && time != nullptr && *promptResult == -2 &&
      KotonohaTimeGet(time) >= endTime) {
    *promptResult = -1;
  }
}

bool Prompt::IsActive() const {
  if (promptResult == nullptr || *promptResult != -2 || time == nullptr)
    return false;
  if (schoolDaysInteraction.Enabled())
    return schoolDaysInteraction.InputOpenAtTick(SchoolDaysCurrentTick());
  return KotonohaTimeGet(time) < endTime;
}

void Prompt::ClearLayoutTextures() {
  for (auto &item : lastLayout) {
    if (item.textTexture != nullptr) {
      SDL_DestroyTexture(item.textTexture);
      item.textTexture = nullptr;
    }
    if (item.shadowTexture != nullptr) {
      SDL_DestroyTexture(item.shadowTexture);
      item.shadowTexture = nullptr;
    }
  }
  lastLayout.clear();
}

void Prompt::ClearSchoolDaysScratchTexture() {
  if (schoolDaysScratchTexture != nullptr) {
    SDL_DestroyTexture(schoolDaysScratchTexture);
    schoolDaysScratchTexture = nullptr;
  }
}

void Prompt::ArmSchoolDaysExit(int selected) {
  if (!schoolDaysInteraction.Enabled() || schoolDaysExitActive) return;
  schoolDaysExitActive = true;
  schoolDaysExitFinalFrameRendered = false;
  schoolDaysExitStartTick = SchoolDaysCurrentTick();
  schoolDaysExitSelected = selected;
  schoolDaysPendingSound = selected >= 0 ? SchoolDaysPendingSound::Select
                                        : SchoolDaysPendingSound::Cancel;
}

void Prompt::PlaySchoolDaysPendingAudio(Audio *audioManager) {
  if (audioManager == nullptr || schoolDaysPendingSound == SchoolDaysPendingSound::None ||
      time == nullptr) {
    return;
  }
  // A click can arrive immediately after a scene swap, before Gameplay::Main
  // has resolved the retail presentation assets.  Preserve the one-shot until
  // that first configuration pass instead of silently dropping SeSelect.
  if (!schoolDaysPresentationConfigured) return;
  const std::string *path = nullptr;
  if (schoolDaysPendingSound == SchoolDaysPendingSound::Select)
    path = &schoolDaysSelectAudioPath;
  else
    path = &schoolDaysCancelAudioPath;
  if (path != nullptr && !path->empty()) {
    audioManager->AddMedia(path->c_str(), KotonohaTimeGet(time), UINT64_MAX,
                           false, "Se");
  }
  schoolDaysPendingSound = SchoolDaysPendingSound::None;
}

bool Prompt::SchoolDaysPresentationTailActive() const {
  if (!schoolDaysInteraction.Enabled() || !schoolDaysExitActive || time == nullptr)
    return false;
  const Kotonoha_SceneTick now = SchoolDaysCurrentTick();
  if (now < schoolDaysExitStartTick) return true;
  const Kotonoha_SceneTick elapsed = now - schoolDaysExitStartTick;
  return elapsed < SchoolDaysChoiceTextPlan::ExitTailTicks(
                       schoolDaysExitSelected >= 0);
}

bool Prompt::SchoolDaysPresentationNeedsFinalFrame() const {
  if (!schoolDaysInteraction.Enabled() || !schoolDaysExitActive ||
      schoolDaysExitFinalFrameRendered || time == nullptr) {
    return false;
  }
  const Kotonoha_SceneTick now = SchoolDaysCurrentTick();
  if (now < schoolDaysExitStartTick) return false;
  return (now - schoolDaysExitStartTick) >=
         SchoolDaysChoiceTextPlan::ExitTailTicks(schoolDaysExitSelected >= 0);
}

bool Prompt::RenderSchoolDays(SDL_Renderer *renderer, int outputWidth,
                              int outputHeight) {
  using Plan = SchoolDaysChoiceTextPlan;
  if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0 ||
      !schoolDaysPresentationConfigured || schoolDaysFontData == nullptr ||
      !schoolDaysFontData->IsLoaded()) {
    return false;
  }

  if (schoolDaysScratchTexture == nullptr) {
    schoolDaysScratchTexture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1024,
        512);
    if (schoolDaysScratchTexture == nullptr ||
        !SDL_SetTextureBlendMode(schoolDaysScratchTexture, SDL_BLENDMODE_BLEND)) {
      ClearSchoolDaysScratchTexture();
      return false;
    }
  }

  std::vector<Uint32> pixels(1024u * 512u, 0u);
  const std::vector<int> visibleIndices = BuildVisiblePromptIndices(prompt);
  const int choiceCount = static_cast<int>(std::min<size_t>(visibleIndices.size(), 2u));
  if (choiceCount <= 0) return true;

  std::vector<std::vector<Plan::Line>> itemLines;
  itemLines.reserve(static_cast<size_t>(choiceCount));
  SchoolDaysFontData::Glyph glyph{};
  std::string decodeError;
  for (int slot = 0; slot < choiceCount; ++slot) {
    auto lines = Plan::WrapRetailEnglish(
        Utf8ToUtf16Prompt(prompt[visibleIndices[static_cast<size_t>(slot)]]));
    if (lines.size() > 5u) return false;
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
      const int baseY = slot * 256 + static_cast<int>(lineIndex) * Plan::kGlyphHeight;
      for (const auto &placement : lines[lineIndex].glyphs) {
        if (!schoolDaysFontData->DecodeGlyph(placement.glyph, &glyph, &decodeError))
          return false;
        for (int gy = 0; gy < Plan::kGlyphHeight; ++gy) {
          const int dstY = baseY + gy;
          if (dstY < 0 || dstY >= 512) continue;
          for (int gx = 0; gx < Plan::kGlyphWidth; ++gx) {
            const int dstX = placement.x + gx;
            if (dstX < 0 || dstX >= 1024) continue;
            const size_t src = static_cast<size_t>(gy * Plan::kGlyphWidth + gx);
            const Uint8 alpha = glyph.alpha[src];
            if (alpha == 0u) continue;
            const Uint8 gray = glyph.gray[src];
            const Uint32 incoming = (static_cast<Uint32>(alpha) << 24u) |
                                    (static_cast<Uint32>(gray) << 16u) |
                                    (static_cast<Uint32>(gray) << 8u) | gray;
            Uint32 &existing = pixels[static_cast<size_t>(dstY) * 1024u +
                                      static_cast<size_t>(dstX)];
            // EXE 0x4367D0: winner-take-all unsigned packed-ARGB max.  Alpha
            // therefore dominates; grayscale breaks ties at equal alpha.
            existing = Plan::CompositeScratchPixel(existing, incoming);
          }
        }
      }
    }
    itemLines.push_back(std::move(lines));
  }

  if (!SDL_UpdateTexture(schoolDaysScratchTexture, nullptr, pixels.data(),
                         1024 * static_cast<int>(sizeof(Uint32)))) {
    return false;
  }

  const float outputAspect = static_cast<float>(outputWidth) /
                             static_cast<float>(outputHeight);
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

  const Kotonoha_SceneTick now = SchoolDaysCurrentTick();
  const uint32_t elapsed = schoolDaysExitActive && now >= schoolDaysExitStartTick
                               ? static_cast<uint32_t>(std::min<Kotonoha_SceneTick>(
                                     now - schoolDaysExitStartTick, UINT32_MAX))
                               : 0u;

  for (int slot = 0; slot < choiceCount; ++slot) {
    const int originalIndex = visibleIndices[static_cast<size_t>(slot)];
    const bool exitSelected = schoolDaysExitActive &&
                              originalIndex == schoolDaysExitSelected;
    Uint32 argb = 0u;
    if (schoolDaysExitActive) {
      argb = Plan::ExitArgb(exitSelected, elapsed);
    } else {
      argb = schoolDaysInteraction.StoredSelection() == originalIndex
                 ? Plan::kSelectedArgb
                 : Plan::kNormalArgb;
    }
    const Uint8 alpha = static_cast<Uint8>((argb >> 24u) & 0xffu);
    if (alpha == 0u) continue;
    SDL_SetTextureColorMod(schoolDaysScratchTexture,
                           static_cast<Uint8>((argb >> 16u) & 0xffu),
                           static_cast<Uint8>((argb >> 8u) & 0xffu),
                           static_cast<Uint8>(argb & 0xffu));
    SDL_SetTextureAlphaMod(schoolDaysScratchTexture, alpha);

    const auto &lines = itemLines[static_cast<size_t>(slot)];
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
      const auto srcPlan = Plan::SourceForLine(slot, static_cast<int>(lineIndex));
      const auto dstPlan = Plan::DestinationForLine(
          choiceCount, slot, static_cast<int>(lineIndex),
          static_cast<int>(lines.size()), lines[lineIndex].horizontalOffset);
      SDL_FRect src{srcPlan.x, srcPlan.y, srcPlan.w, srcPlan.h};
      SDL_FRect dst{content.x + dstPlan.x * content.w,
                    content.y + dstPlan.y * content.h,
                    dstPlan.w * content.w, dstPlan.h * content.h};
      if (!SDL_RenderTexture(renderer, schoolDaysScratchTexture, &src, &dst))
        return false;
    }
  }
  return true;
}

void Prompt::HandleEvent(SDL_Event *event, struct Kotonoha_Game *gameContext,
                         Audio *audioManager) {
  if (schoolDaysInteraction.Enabled()) {
    if (event == nullptr || promptResult == nullptr || *promptResult != -2 ||
        !SchoolDaysPromptInteraction::AcceptsEventType(event->type)) {
      return;
    }
    Update();
    if (*promptResult != -2 || gameContext == nullptr ||
        gameContext->render == nullptr) {
      return;
    }

    float windowX = 0.0f;
    float windowY = 0.0f;
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
      windowX = event->motion.x;
      windowY = event->motion.y;
    } else {
      if (event->button.button != SDL_BUTTON_LEFT) {
        return;
      }
      windowX = event->button.x;
      windowY = event->button.y;
    }

    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    if (!SchoolDaysPointFromWindow(gameContext->render, windowX, windowY,
                                   &normalizedX, &normalizedY)) {
      return;
    }

    const Kotonoha_SceneTick currentTick = SchoolDaysCurrentTick();
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
      schoolDaysInteraction.PointerMotion(normalizedX, normalizedY, currentTick);
    } else {
      if (schoolDaysInteraction.LeftClick(normalizedX, normalizedY, currentTick,
                                          promptResult)) {
        ArmSchoolDaysExit(*promptResult);
        PlaySchoolDaysPendingAudio(audioManager);
      }
    }
    return;
  }

  if (event == nullptr || promptResult == nullptr || *promptResult != -2 ||
      lastLayout.empty()) {
    return;
  }

  if (time != nullptr && KotonohaTimeGet(time) >= endTime) {
    *promptResult = -1;
    ClearLayoutTextures();
    return;
  }

  float rawX = -1.0f;
  float rawY = -1.0f;
  bool isClick = false;

  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event->button.button == SDL_BUTTON_LEFT) {
    rawX = event->button.x;
    rawY = event->button.y;
    isClick = true;
  } else if (event->type == SDL_EVENT_FINGER_DOWN && gameContext != nullptr &&
             gameContext->window != nullptr) {
    int w = 0, h = 0;
    // No SDL3, SDL_GetWindowSizeInPixels lida melhor com telas de alta
    // densidade (Retina/DPI)
    SDL_GetWindowSizeInPixels(gameContext->window, &w, &h);
    rawX = event->tfinger.x * static_cast<float>(w);
    rawY = event->tfinger.y * static_cast<float>(h);
    isClick = true;
  }

  if (!isClick) {
    return;
  }

  // Se usamos SDL_GetWindowSizeInPixels, as coordenadas já estão em pixels
  // reais de renderização. Caso contrário, aplicamos a escala se necessário.
  float windowScale = 1.0f;
  if (gameContext != nullptr && gameContext->window != nullptr &&
      event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    windowScale = SDL_GetWindowDisplayScale(gameContext->window);
  }

  const float clickX =
      (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && windowScale > 0.0f)
          ? (rawX * windowScale)
          : rawX;
  const float clickY =
      (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && windowScale > 0.0f)
          ? (rawY * windowScale)
          : rawY;

  for (const auto &item : lastLayout) {
    const bool hit =
        clickX >= item.rect.x && clickX <= (item.rect.x + item.rect.w) &&
        clickY >= item.rect.y && clickY <= (item.rect.y + item.rect.h);

    if (hit) {
      *promptResult = item.originalIndex;

      if (audioManager != nullptr && !audioPath.empty() && time != nullptr) {
        const Uint64 nowMs = KotonohaTimeGet(time);
        audioManager->AddMedia(audioPath.c_str(), nowMs, UINT64_MAX, false,
                               "Se");
      }
      break;
    }
  }
}

enum Kotonoha_Scene_Status Prompt::Render(KOTONOHA_SCENE_CALL) {
  if (userData == nullptr || render == nullptr) {
    return KOTONOHA_SCENE_NULL;
  }

  auto *promptObj = static_cast<Prompt *>(userData);

  if (promptObj->time == nullptr || promptObj->promptResult == nullptr ||
      (!promptObj->schoolDaysInteraction.Enabled() && promptObj->font == nullptr)) {
    return KOTONOHA_SCENE_NULL;
  }

  promptObj->Update();

  bool inRange = false;
  Sint64 diff = 0;
  KotonohaTimeGetFromEvent(promptObj->time, promptObj->startTime,
                           promptObj->endTime, &inRange, &diff);

  if (promptObj->schoolDaysInteraction.Enabled()) {
    const Kotonoha_SceneTick now = promptObj->SchoolDaysCurrentTick();
    if (!promptObj->schoolDaysInteraction.VisibleAtTick(now) &&
        !promptObj->schoolDaysExitActive)
      return KOTONOHA_SCENE_NULL;
    bool finalExitFrame = false;
    if (promptObj->schoolDaysExitActive && now >= promptObj->schoolDaysExitStartTick) {
      const Kotonoha_SceneTick elapsed = now - promptObj->schoolDaysExitStartTick;
      const Kotonoha_SceneTick tail = SchoolDaysChoiceTextPlan::ExitTailTicks(
          promptObj->schoolDaysExitSelected >= 0);
      finalExitFrame = elapsed >= tail;
    }

    if (!promptObj->schoolDaysInteraction.Seeded() &&
        *(promptObj->promptResult) == -2) {
      float rawMouseX = 0.0f;
      float rawMouseY = 0.0f;
      SDL_GetMouseState(&rawMouseX, &rawMouseY);
      float normalizedX = 0.0f;
      float normalizedY = 0.0f;
      if (promptObj->SchoolDaysPointFromWindow(render, rawMouseX, rawMouseY,
                                               &normalizedX, &normalizedY)) {
        promptObj->schoolDaysInteraction.SeedPointer(normalizedX, normalizedY,
                                                     now);
      }
    }

    int outputWidth = 0;
    int outputHeight = 0;
    if (!SDL_GetCurrentRenderOutputSize(render, &outputWidth, &outputHeight) ||
        outputWidth <= 0 || outputHeight <= 0) {
      return KOTONOHA_SCENE_NULL;
    }
    Uint8 oldR = 0, oldG = 0, oldB = 0, oldA = 0;
    SDL_GetRenderDrawColor(render, &oldR, &oldG, &oldB, &oldA);
    SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
    SDL_RenderClear(render);
    // Missing/malformed authenticated resources fail closed; never fall back
    // to SDL_ttf for the School Days policy branch.
    if (!finalExitFrame) {
      promptObj->RenderSchoolDays(render, outputWidth, outputHeight);
    } else {
      // The native exit reaches alpha zero at exactly tick 24/48. Publish the
      // transparent overlay once so an outgoing-frame lease captures that
      // terminal composition instead of the prior alpha-11 choice frame.
      promptObj->schoolDaysExitFinalFrameRendered = true;
    }
    SDL_SetRenderDrawColor(render, oldR, oldG, oldB, oldA);
    return KOTONOHA_SCENE_DRAW_OVERLAYED;
  }

  if (!inRange) {
    promptObj->ClearLayoutTextures();
    return KOTONOHA_SCENE_NULL;
  }

  const std::vector<int> visibleIndices =
      BuildVisiblePromptIndices(promptObj->prompt);

  if (visibleIndices.empty()) {
    promptObj->ClearLayoutTextures();
    return KOTONOHA_SCENE_DRAW_OVERLAYED;
  }

  TTF_SetFontSize(promptObj->font, 32);

  int outputWidth = 0;
  int outputHeight = 0;
  float windowScale = 1.0f;

  if (!KotonohaOverlayGetMetrics(window, render, &outputWidth, &outputHeight,
                                 &windowScale)) {
    return KOTONOHA_SCENE_NULL;
  }

  float rawMouseX = 0.0f;
  float rawMouseY = 0.0f;
  SDL_GetMouseState(&rawMouseX, &rawMouseY);

  const float mouseX =
      (windowScale > 0.0f) ? (rawMouseX * windowScale) : rawMouseX;
  const float mouseY =
      (windowScale > 0.0f) ? (rawMouseY * windowScale) : rawMouseY;

  const bool isMouseDown = (SDL_GetMouseState(nullptr, nullptr) &
                            SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;

  const int centerY = outputHeight / 2;
  const int halfW = outputWidth / 2;
  const int paddingX = static_cast<int>(24.0f * windowScale);
  const int paddingY = static_cast<int>(16.0f * windowScale);

  static int lastOutputW = 0;
  static int lastOutputH = 0;
  const bool targetResized =
      (lastOutputW != outputWidth || lastOutputH != outputHeight);

  if (targetResized) {
    promptObj->ClearLayoutTextures();
    lastOutputW = outputWidth;
    lastOutputH = outputHeight;
  }

  if (promptObj->lastLayout.size() != visibleIndices.size()) {
    promptObj->ClearLayoutTextures();
    promptObj->lastLayout.reserve(visibleIndices.size());

    if (visibleIndices.size() == 1) {
      const int idx = visibleIndices[0];
      int textW = 0, textH = 0; // Usando int* para TTF_GetStringSize
      TTF_GetStringSize(promptObj->font, promptObj->prompt[idx].c_str(),
                        promptObj->prompt[idx].size(), &textW, &textH);

      SDL_FRect rect{};
      rect.w = (static_cast<float>(textW) + static_cast<float>(paddingX * 2)) *
               windowScale;
      rect.h = (static_cast<float>(textH) + static_cast<float>(paddingY * 2)) *
               windowScale;
      rect.x = (static_cast<float>(outputWidth) - rect.w) / 2.0f;
      rect.y = (static_cast<float>(outputHeight) - rect.h) / 2.0f;

      PromptLayoutItem item{};
      item.originalIndex = idx;
      item.rect = rect;
      promptObj->lastLayout.push_back(item);
    } else {
      const size_t count =
          visibleIndices.size() > 2 ? 2 : visibleIndices.size();
      for (size_t slot = 0; slot < count; ++slot) {
        const int idx = visibleIndices[slot];
        int textW = 0, textH = 0; // Usando int* para TTF_GetStringSize
        TTF_GetStringSize(promptObj->font, promptObj->prompt[idx].c_str(),
                          promptObj->prompt[idx].size(), &textW, &textH);

        SDL_FRect rect{};
        rect.w =
            (static_cast<float>(textW) + static_cast<float>(paddingX * 2)) *
            windowScale;
        rect.h =
            (static_cast<float>(textH) + static_cast<float>(paddingY * 2)) *
            windowScale;

        const float regionX =
            (slot == 0) ? 0.0f : static_cast<float>(halfW);
        const float regionW = static_cast<float>(halfW);
        rect.x = regionX + (regionW - rect.w) / 2.0f;
        rect.y = static_cast<float>(centerY) - (rect.h / 2.0f);

        PromptLayoutItem item{};
        item.originalIndex = idx;
        item.rect = rect;
        promptObj->lastLayout.push_back(item);
      }
    }
  }

  for (auto &item : promptObj->lastLayout) {
    const bool hovered =
        mouseX >= item.rect.x && mouseX <= (item.rect.x + item.rect.w) &&
        mouseY >= item.rect.y && mouseY <= (item.rect.y + item.rect.h);

    const bool selected = (*(promptObj->promptResult) == item.originalIndex);
    const bool clicking =
        hovered && isMouseDown && (*(promptObj->promptResult) == -2);

    const SDL_Color fg = clicking   ? SDL_Color{255, 140, 0, 255}
                         : selected ? SDL_Color{255, 230, 120, 255}
                         : hovered  ? SDL_Color{180, 220, 255, 255}
                                    : SDL_Color{255, 255, 255, 255};

    SDL_Color shadowColor = {0, 0, 0, static_cast<Uint8>(hovered ? 200 : 160)};

    if (item.textTexture != nullptr) {
      SDL_DestroyTexture(item.textTexture);
      item.textTexture = nullptr;
    }
    if (item.shadowTexture != nullptr) {
      SDL_DestroyTexture(item.shadowTexture);
      item.shadowTexture = nullptr;
    }

    float textW = 0.0f;
    float textH = 0.0f;

    const std::string &txt = promptObj->prompt[item.originalIndex];

    if (KotonohaOverlayCreateTextTexture(render, promptObj->font, txt.c_str(),
                                         shadowColor, &item.shadowTexture,
                                         &textW, &textH)) {
      item.shadowW = static_cast<int>(textW * windowScale);
      item.shadowH = static_cast<int>(textH * windowScale);
    }

    if (KotonohaOverlayCreateTextTexture(render, promptObj->font, txt.c_str(),
                                         fg, &item.textTexture, &textW,
                                         &textH)) {
      item.textW = static_cast<int>(textW * windowScale);
      item.textH = static_cast<int>(textH * windowScale);
    }

    if (item.shadowTexture != nullptr) {
      SDL_SetTextureBlendMode(item.shadowTexture, SDL_BLENDMODE_BLEND);
      SDL_FRect shadowRect = {
          item.rect.x + static_cast<float>(paddingX) + (3.0f * windowScale),
          item.rect.y + static_cast<float>(paddingY) + (3.0f * windowScale),
          static_cast<float>(item.shadowW), static_cast<float>(item.shadowH)};
      SDL_RenderTexture(render, item.shadowTexture, nullptr, &shadowRect);
    }

    if (item.textTexture != nullptr) {
      SDL_FRect textRect = {item.rect.x + static_cast<float>(paddingX),
                            item.rect.y + static_cast<float>(paddingY),
                            static_cast<float>(item.textW),
                            static_cast<float>(item.textH)};
      SDL_RenderTexture(render, item.textTexture, nullptr, &textRect);
    }
  }

  return KOTONOHA_SCENE_DRAW_OVERLAYED;
}

void Prompt::Reset() {
  if (promptResult != nullptr) {
    *promptResult = -2;
  }
  schoolDaysInteraction.ResetSelection();
  schoolDaysExitActive = false;
  schoolDaysExitFinalFrameRendered = false;
  schoolDaysExitStartTick = 0;
  schoolDaysExitSelected = -1;
  schoolDaysPendingSound = SchoolDaysPendingSound::None;
  ClearLayoutTextures();
}

} // namespace Kotonoha
