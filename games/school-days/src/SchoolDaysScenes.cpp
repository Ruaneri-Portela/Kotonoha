#include "SchoolDaysScenes.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace SchoolDays {
namespace {

std::string Utf16ToUtf8(const std::u16string &text) {
  if (text.empty())
    return {};
  char *converted = SDL_iconv_string(
      "UTF-8", "UTF-16LE", reinterpret_cast<const char *>(text.c_str()),
      (text.size() + 1u) * sizeof(char16_t));
  if (!converted)
    return {};
  std::string result(converted);
  SDL_free(converted);
  return result;
}
} // namespace

LogoSplash::LogoSplash(Kotonoha::Engine &engine, Kotonoha::Providers::SchoolDaysAssets &source)
    : Scene(engine), assets(source) {
  for (const char *stem : {"System/SYSTEM/LOGO", "System/System/Logo"})
    try {
      // A loose game overlay may supply the logo even when the retail
      // installation and synthetic fixtures have no matching archive entry.
      const std::string overlayLogo = std::string(stem) + ".png";
      if (!assets.overlayPath(overlayLogo).empty()) {
        logo = overlayLogo;
        break;
      }
      logo = assets.resolve(stem, true);
      break;
    } catch (const std::exception &) {
    }
  if (logo.empty())
    SDL_LogWarn(Kotonoha::LogUI,
                "School Days logo System/SYSTEM/LOGO.png is unavailable");
  restart();
}

void LogoSplash::restart() {
  done = logo.empty();
  shown = false;
  started = SDL_GetTicksNS();
}

double LogoSplash::elapsed() const {
  return double(SDL_GetTicksNS() - started) / SDL_NS_PER_SECOND;
}

bool LogoSplash::handleEvent(Kotonoha::EventRef event) {
  if (done)
    return false;
  const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.which != SDL_TOUCH_MOUSEID;
  const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
  if (click || key || event.type == SDL_EVENT_FINGER_DOWN)
    done = true;
  return true;
}

void LogoSplash::update() {
  // The timeout also covers a logo that never decodes.
  if (!done && elapsed() >= displaySeconds + (shown ? 0.0 : fadeSeconds))
    done = true;
}

void LogoSplash::render(Kotonoha::RendererRef renderer, float) {
  if (done)
    return;
  int outputWidth = 0, outputHeight = 0;
  SDL_RendererLogicalPresentation mode{};
  SDL_GetRenderLogicalPresentation(&renderer, &outputWidth, &outputHeight,
                                   &mode);
  if (outputWidth <= 0 || outputHeight <= 0)
    SDL_GetCurrentRenderOutputSize(&renderer, &outputWidth, &outputHeight);
  const SDL_FRect area{0, 0, static_cast<float>(std::max(outputWidth, 1)),
                       static_cast<float>(std::max(outputHeight, 1))};
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(&renderer, &area);
  SDL_Texture *texture = nullptr;
  try {
    texture = &assets.texture(renderer, logo);
  } catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days logo: %s", error.what());
    done = true;
    return;
  }
  if (!shown) {
    shown = true;
    started = SDL_GetTicksNS();
  }
  // The retail host explicitly enabled blending for every splash texture.
  // An opaque source PNG otherwise ignores the animated alpha modulation.
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  float width = 0, height = 0;
  SDL_GetTextureSize(texture, &width, &height);
  if (width <= 0 || height <= 0)
    return;
  const float fit = std::min(area.w / width, area.h / height);
  const SDL_FRect target{(area.w - width * fit) / 2,
                         (area.h - height * fit) / 2, width * fit,
                         height * fit};
  const double time = elapsed();
  const double alpha =
      std::clamp(std::min(time, displaySeconds - time) / fadeSeconds, 0.0, 1.0);
  SDL_SetTextureAlphaModFloat(texture, float(alpha));
  SDL_RenderTexture(&renderer, texture, nullptr, &target);
  SDL_SetTextureAlphaModFloat(texture, 1.0f);
}

std::string Subtitles::displayCueText(const std::string &text) const {
  return text;
}

void Subtitles::bind(const Kotonoha_orsData &script,
                     const std::string &section) {
  std::vector<Kotonoha::SubtitleCue> cues;
  originalCues.clear();
  for (auto *event = script.data; event; event = event->next)
    if (event->command == PRINT_TEXT && event->data.print_text &&
        event->data.print_text->text && event->endTick > event->startTick) {
      const auto &text = *event->data.print_text;
      const std::string original = text.text;
      Kotonoha::SubtitleCue cue{event->startTick / 24.0, event->endTick / 24.0,
                                original, text.character ? text.character : "",
                                true};
      originalCues.push_back(cue);
      cue.text = engine.GetTranslation().text(section, original);
      cues.push_back(std::move(cue));
    }
  setCues(cues, 46);
  setPosition(0);
}

std::vector<std::string> Subtitles::originalCueTexts() const {
  std::vector<std::string> result;
  result.reserve(originalCues.size());
  for (const auto &cue : originalCues)
    result.push_back(cue.text);
  return result;
}

std::vector<std::string> Subtitles::currentOriginalCueTexts() const {
  std::vector<std::string> result;
  const double position = getPosition();
  for (const auto &cue : originalCues)
    if (cue.start <= position && position < cue.end)
      result.push_back(cue.text);
  return result;
}

GameScene::GameScene(Kotonoha::Engine &engine, Runtime &runtime)
    : Scene(engine), runtime(runtime) {
  try {
    const auto &fontKey = engine.GetSettings().fontPath;
    const auto fontPath = fontKey.empty()
                              ? Kotonoha::FileResolver::font()
                              : Kotonoha::FileResolver::font(fontKey);
    choiceFont.reset(
        TTF_OpenFont(fontPath.string().c_str(),
                     Kotonoha::SchoolDaysSubtitleTypography::PointSize));
    if (!choiceFont)
      SDL_LogWarn(Kotonoha::LogUI, "School Days choice font unavailable: %s",
                  SDL_GetError());
  } catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days choice font unavailable: %s",
                error.what());
  }
}

bool GameScene::ensureMenuBar() {
  if (menuBar.IsOpen() && menuBarVariant == runtime.uiVariant)
    return true;
  if (menuBar.IsOpen()) {
    menuBar.Close();
    menuBarAttempted = false;
  }
  if (menuBarAttempted || runtime.currentState != Kotonoha::GameState::Gameplay)
    return false;
  menuBarAttempted = true;

  int64_t feeling001 = 0;
  int64_t feeling002 = 0;
  Kotonoha_KtrfError stateError{};
  if (!runtime.bridge.Session().State().ReadSessionInt("001", &feeling001,
                                                       &stateError) ||
      !runtime.bridge.Session().State().ReadSessionInt("002", &feeling002,
                                                       &stateError) ||
      feeling001 < std::numeric_limits<int>::min() ||
      feeling001 > std::numeric_limits<int>::max() ||
      feeling002 < std::numeric_limits<int>::min() ||
      feeling002 > std::numeric_limits<int>::max()) {
    SDL_LogWarn(Kotonoha::LogUI,
                "School Days MenuBar feeling state unavailable: %s",
                stateError.message);
    return false;
  }

  std::string error;
  if (!menuBar.Open(&engine.GetRenderer(), runtime.assets.get(),
                    runtime.assets->root().string(), runtime.uiVariant,
                    static_cast<int>(feeling001), static_cast<int>(feeling002),
                    &error)) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days MenuBar unavailable: %s",
                error.c_str());
    return false;
  }
  menuBarVariant = runtime.uiVariant;
  return true;
}

void GameScene::syncChoice() {
  if (runtime.choice != lastChoice) {
    lastChoice = runtime.choice;
    choiceScratch.reset();
    choiceScratchEvent = nullptr;
  }
}
std::vector<int> GameScene::choices() const {
  std::vector<int> result;
  if (runtime.choice && (runtime.choicePresentation.exitActive ||
                         runtime.choiceInteraction.VisibleAtTick(runtime.tick)))
    for (Uint64 i = 0; i < runtime.choice->data.set_select->size; ++i) {
      const char *text = runtime.choice->data.set_select->options[i];
      if (text && *text)
        result.push_back(static_cast<int>(i));
    }
  return result;
}

bool GameScene::normalizedPointer(float windowX, float windowY, float *x,
                                  float *y) const {
  if (x == nullptr || y == nullptr)
    return false;
  // Engine::Event already converts scene events into Novel coordinates.
  const auto view = Kotonoha::UI::viewSize(engine.GetRenderer());
  if (!(view.x > 0.0f) || !(view.y > 0.0f))
    return false;
  *x = windowX / view.x;
  *y = windowY / view.y;
  return true;
}

bool GameScene::ensureChoiceScratch(Kotonoha::RendererRef renderer,
                                    const std::vector<int> &visible) {
  if (choiceScratch && choiceScratchEvent == runtime.choice)
    return true;
  choiceScratch.reset();
  choiceScratchEvent = nullptr;
  if (!choiceFont || runtime.choice == nullptr || visible.empty() ||
      visible.size() > 2u)
    return false;

  Kotonoha::SurfacePtr scratch{
      SDL_CreateSurface(1024, 512, SDL_PIXELFORMAT_RGBA32)};
  if (!scratch || !SDL_FillSurfaceRect(scratch.get(), nullptr, 0u))
    return false;

  using Plan = Kotonoha::SchoolDaysChoiceTextPlan;
  for (std::size_t slot = 0; slot < visible.size(); ++slot) {
    const char *raw = runtime.choice->data.set_select->options[visible[slot]];
    const auto lines =
        Plan::WrapRetailEnglish(Kotonoha::Utf8ToUtf16(raw ? raw : ""));
    if (lines.size() > 5u)
      return false;
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
      const std::string utf8 = Utf16ToUtf8(lines[lineIndex].text);
      if (utf8.empty())
        continue;
      Kotonoha::SurfacePtr rendered{
          Kotonoha::SchoolDaysSubtitleTypography::Render(
              choiceFont.get(), utf8.c_str(), utf8.size())};
      if (!rendered)
        return false;
      const int measuredWidth =
          std::max(1, Plan::Measure(lines[lineIndex].text));
      SDL_Rect destination{0,
                           static_cast<int>(slot) * 256 +
                               static_cast<int>(lineIndex) * Plan::kGlyphHeight,
                           std::min(measuredWidth, 1024), Plan::kGlyphHeight};
      if (!SDL_BlitSurfaceScaled(rendered.get(), nullptr, scratch.get(),
                                 &destination, SDL_SCALEMODE_LINEAR))
        return false;
    }
  }

  choiceScratch.reset(SDL_CreateTextureFromSurface(&renderer, scratch.get()));
  if (!choiceScratch ||
      !SDL_SetTextureBlendMode(choiceScratch.get(), SDL_BLENDMODE_BLEND)) {
    choiceScratch.reset();
    return false;
  }
  choiceScratchEvent = runtime.choice;
  return true;
}

void GameScene::renderChoices(Kotonoha::RendererRef renderer) {
  using Plan = Kotonoha::SchoolDaysChoiceTextPlan;
  const auto visible = choices();
  if (visible.empty())
    return;

  if (!runtime.committed && !runtime.choiceInteraction.Seeded()) {
    float mx = 0.0f, my = 0.0f, nx = 0.0f, ny = 0.0f;
    SDL_GetMouseState(&mx, &my);
    float renderX = 0.0f, renderY = 0.0f;
    if (SDL_RenderCoordinatesFromWindow(&renderer, mx, my, &renderX,
                                        &renderY)) {
      // The first-hover seed reads the physical cursor directly, bypassing
      // Engine's normal mouse-event mapping. Apply the exact inverse of the
      // Standard/FULL vertical presentation to keep the initial option hover
      // aligned with later clicks and with the compressed choice artwork.
      const float heightScale = runtime.narrativePresentationHeightScale();
      const auto view = Kotonoha::UI::viewSize(renderer);
      if (heightScale > 0.0f && heightScale < 1.0f)
        renderY =
            (renderY - view.y * (1.0f - heightScale) * 0.5f) / heightScale;
      if (normalizedPointer(renderX, renderY, &nx, &ny))
        runtime.choiceInteraction.SeedPointer(nx, ny, runtime.tick);
    }
  }

  const bool finalFrame = runtime.choiceNeedsFinalFrame();
  if (!ensureChoiceScratch(renderer, visible)) {
    if (finalFrame)
      runtime.choicePresentation.finalFrameRendered = true;
    return;
  }

  const auto view = Kotonoha::UI::viewSize(renderer);
  constexpr float kAspect = 16.0f / 9.0f;
  SDL_FRect content{0.0f, 0.0f, view.x, view.y};
  const float outputAspect = view.x / view.y;
  if (outputAspect > kAspect) {
    content.w = content.h * kAspect;
    content.x = (view.x - content.w) * 0.5f;
  } else if (outputAspect < kAspect) {
    content.h = content.w / kAspect;
    content.y = (view.y - content.h) * 0.5f;
  }

  const uint32_t elapsed =
      runtime.choicePresentation.exitActive &&
              runtime.tick >= runtime.choicePresentation.exitStartTick
          ? static_cast<uint32_t>(std::min<Kotonoha_SceneTick>(
                runtime.tick - runtime.choicePresentation.exitStartTick,
                UINT32_MAX))
          : 0u;

  bool rendered = true;
  for (std::size_t slot = 0; slot < visible.size(); ++slot) {
    const int originalIndex = visible[slot];
    const bool selected =
        runtime.choicePresentation.exitActive &&
        originalIndex == runtime.choicePresentation.exitSelected;
    const Uint32 argb =
        runtime.choicePresentation.exitActive
            ? Plan::ExitArgb(selected, elapsed)
        : runtime.choiceInteraction.StoredSelection() == static_cast<int>(slot)
            ? Plan::kSelectedArgb
            : Plan::kNormalArgb;
    const Uint8 alpha = static_cast<Uint8>((argb >> 24u) & 0xffu);
    if (alpha == 0u && !finalFrame)
      continue;
    SDL_SetTextureColorMod(choiceScratch.get(),
                           static_cast<Uint8>((argb >> 16u) & 0xffu),
                           static_cast<Uint8>((argb >> 8u) & 0xffu),
                           static_cast<Uint8>(argb & 0xffu));
    SDL_SetTextureAlphaMod(choiceScratch.get(), alpha);

    const char *raw = runtime.choice->data.set_select->options[originalIndex];
    const auto lines =
        Plan::WrapRetailEnglish(Kotonoha::Utf8ToUtf16(raw ? raw : ""));
    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
      const auto source = Plan::SourceForLine(static_cast<int>(slot),
                                              static_cast<int>(lineIndex));
      const auto destination = Plan::DestinationForLine(
          static_cast<int>(visible.size()), static_cast<int>(slot),
          static_cast<int>(lineIndex), static_cast<int>(lines.size()),
          lines[lineIndex].horizontalOffset);
      const SDL_FRect src{source.x, source.y, source.w, source.h};
      const SDL_FRect dst{content.x + destination.x * content.w,
                          content.y + destination.y * content.h,
                          destination.w * content.w, destination.h * content.h};
      rendered =
          SDL_RenderTexture(&renderer, choiceScratch.get(), &src, &dst) &&
          rendered;
    }
  }
  SDL_SetTextureColorMod(choiceScratch.get(), 255, 255, 255);
  SDL_SetTextureAlphaMod(choiceScratch.get(), 255);
  if (finalFrame && rendered) {
    runtime.choicePresentation.finalFrameRendered = true;
    runtime.traceDev("ChoiceFinalFrame");
    // Engine::Iterate snapshots the complete owned Novel target before Present.
  }
}

void GameScene::update() {
  syncChoice();
  if (ensureMenuBar())
    runtime.updateMenuBar(menuBar, static_cast<uint32_t>(SDL_GetTicks()));
}

bool GameScene::handleEvent(Kotonoha::EventRef event) {
  syncChoice();
  if (runtime.modal())
    return false;

  const auto visible = choices();
#if defined(__ANDROID__) || defined(ANDROID)
  const bool choiceTouch = event.type == SDL_EVENT_FINGER_MOTION ||
                           event.type == SDL_EVENT_FINGER_DOWN;
#else
  const bool choiceTouch = false;
#endif
  if (!visible.empty() && !runtime.committed &&
      (Kotonoha::SchoolDaysPromptInteraction::AcceptsEventType(event.type) ||
       choiceTouch)) {
    float windowX = 0.0f, windowY = 0.0f;
    if (choiceTouch) {
      windowX = event.tfinger.x;
      windowY = event.tfinger.y;
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
      windowX = event.motion.x;
      windowY = event.motion.y;
    } else {
      if (event.button.button != SDL_BUTTON_LEFT)
        return false;
      windowX = event.button.x;
      windowY = event.button.y;
    }
    float x = 0.0f, y = 0.0f;
    if (!normalizedPointer(windowX, windowY, &x, &y))
      return false;
    if (event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_FINGER_MOTION) {
      runtime.choiceInteraction.PointerMotion(x, y, runtime.tick);
      return true;
    }

    runtime.backAtSceneStart = false;
    int result = runtime.choicePresentation.result;
    if (runtime.choiceInteraction.LeftClick(x, y, runtime.tick, &result)) {
      runtime.choicePresentation.result = result;
      if (result >= 0 && static_cast<std::size_t>(result) < visible.size())
        runtime.choose(visible[static_cast<std::size_t>(result)]);
      else
        runtime.choose(result);
    }
    // A stale click is still a prompt click; native does not leak it through to
    // narrative handlers or synthesize a new hover selection from button-down.
    return true;
  }

  const bool handoffClick = event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                            event.button.button == SDL_BUTTON_LEFT;
  if (handoffClick)
    runtime.backAtSceneStart = false;
  return false;
}

void GameScene::render(Kotonoha::RendererRef renderer, float) {
  renderChoices(renderer);
}

bool GameScene::handleMenuBarEvent(Kotonoha::EventRef event) {
  if (runtime.modal() || !ensureMenuBar())
    return false;
  Kotonoha::SchoolDaysMenuBarPresenter::Action action;
  const bool consumed =
      menuBar.HandleEvent(&engine.GetRenderer(), &event, &action);
  if (action.kind != Kotonoha::SchoolDaysMenuBarPresenter::ActionKind::None) {
    runtime.activateMenuAction(action);
    return true;
  }
  return consumed;
}

void GameScene::renderMenuBar(Kotonoha::RendererRef renderer) {
  if (menuBar.IsOpen()) {
    std::string error;
    if (!menuBar.Render(&renderer, &error)) {
      SDL_LogWarn(Kotonoha::LogUI, "School Days MenuBar render failed: %s",
                  error.c_str());
      menuBar.Close();
    }
  }
}
} // namespace SchoolDays
