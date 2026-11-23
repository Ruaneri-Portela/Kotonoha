#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

VNTextBox::VNTextBox(Engine &engineRef, std::string speaker,
                     std::string dialogue, double charsPerSecond)
    : Scene(engineRef) {
  setTextSpeed(charsPerSecond);
  // Constructors cannot dispatch to a subclass yet: use the base behaviour.
  VNTextBox::setDialogue(speaker, dialogue);
}

void VNTextBox::completeDialogue() {
  visibleChars =
      static_cast<double>(VNTextBoxUtility::CountUtf8Chars(fullDialogue));
  visibleDialogue = fullDialogue;
  isComplete = true;
}

void VNTextBox::replaceTranslation(const std::string &original,
                                   const std::string &previous,
                                   const std::string &translated) {
  const auto replace = [&](std::string &text) {
    if (text == previous || text == original) {
      text = translated;
      return true;
    }
    return false;
  };
  replace(speakerName);
  if (replace(fullDialogue)) {
    layoutValid_ = false;
    if (isComplete) {
      visibleDialogue = fullDialogue;
      visibleChars = VNTextBoxUtility::CountUtf8Chars(fullDialogue);
    } else {
      visibleChars = std::min(
          visibleChars, double(VNTextBoxUtility::CountUtf8Chars(fullDialogue)));
      visibleDialogue = fullDialogue.substr(
          0, utf8Prefix(fullDialogue, size_t(visibleChars)));
    }
  }
}

void VNTextBox::setTextSpeed(double charsPerSecond) {
  if (!std::isfinite(charsPerSecond) || charsPerSecond <= 0)
    throw std::invalid_argument("Text speed must be positive and finite");
  cps = charsPerSecond;
}

void VNTextBox::setDialogue(const std::string &speaker,
                            const std::string &dialogue) {
  speakerName = speaker;
  fullDialogue = dialogue;
  layoutValid_ = false;
  visibleDialogue.clear();
  visibleChars = 0;
  isComplete = fullDialogue.empty();
  clock.reset();
  recordDialogue(speakerName, fullDialogue);
}

void VNTextBox::recordDialogue(const std::string &speaker,
                               const std::string &dialogue) {
  GetVNBacklogRuntime(engine).addEntry(speaker, dialogue);
}

void VNTextBox::openMenu() { GetInGameMenuRuntime(engine).open(); }
void VNTextBox::openBacklog() { GetVNBacklogRuntime(engine).open(); }

void VNTextBox::setUserHidden(bool hidden) {
  userHidden = hidden;
  clock.reset();
}

bool VNTextBox::onVisibilityEvent(EventRef rawEvent) {
  if (!visible)
    return false;
  const auto event = MapControllerEvent(rawEvent);
  const bool keyDown = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
  const bool mouseDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  const bool pointerActivation =
      mouseDown || (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                    event.button.which == SDL_TOUCH_MOUSEID);
  if ((keyDown && event.key.key == SDLK_H) ||
      (mouseDown && event.button.button == SDL_BUTTON_MIDDLE)) {
    toggleUserHidden();
    return true;
  }
  if (userHidden &&
      ((keyDown &&
        (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE ||
         event.key.key == SDLK_ESCAPE)) ||
       (pointerActivation && (event.button.button == SDL_BUTTON_LEFT ||
                              event.button.button == SDL_BUTTON_RIGHT)))) {
    setUserHidden(false);
    return true;
  }
  return false;
}

bool VNTextBox::onEvent(EventRef rawEvent) {
  if (!visible)
    return false;
  const auto event = MapControllerEvent(rawEvent);
  if (!touchActivation.accept(event))
    return true;
  if (onVisibilityEvent(event))
    return true;
  if (userHidden)
    return false;
  if (UI::clicked(event, {730, 506, 160, 44})) {
    setUserHidden(true);
    return true;
  }
  if (BackAction(engine, event) || UI::clicked(event, {900, 506, 160, 44})) {
    openMenu();
    return true;
  }
  if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
       (event.key.key == SDLK_B || event.key.key == SDLK_PAGEUP)) ||
      UI::clicked(event, {1070, 506, 160, 44})) {
    openBacklog();
    return true;
  }
  return false;
}

bool VNTextBox::handleEvent(EventRef event) { return onEvent(event); }

void VNTextBox::update() {
  const double delta = clock.seconds();
  clock.reset();
  if (!isVisible() || isComplete || engine.isStoryPaused())
    return;
  if (engine.GetSettings().vnTextInstant) {
    completeDialogue();
    return;
  }
  const double rate = engine.GetSettings().vnTextSpeed > 0
                          ? engine.GetSettings().vnTextSpeed
                          : charactersPerSecond();
  const std::size_t totalChars = VNTextBoxUtility::CountUtf8Chars(fullDialogue);
  visibleChars =
      std::min(visibleChars + rate * delta, static_cast<double>(totalChars));
  const auto bytes =
      utf8Prefix(fullDialogue, static_cast<std::size_t>(visibleChars));
  visibleDialogue = fullDialogue.substr(0, bytes);
  isComplete = (visibleChars >= totalChars);
}

void VNTextBox::render(RendererRef renderer, float) {
  if (!isVisible())
    return;
  renderFrame(renderer);
  renderSpeaker(renderer);
  renderDialogue(renderer);
  if (isComplete)
    renderIndicator(renderer);
}

void VNTextBox::renderFrame(RendererRef renderer) {
  auto &ui = engine.GetUI();
  ui.panel(renderer, {40, 496, 1200, 204});
  ui.button(renderer, {730, 506, 160, 44}, "Hide [H]");
  ui.button(renderer, {900, 506, 160, 44}, "Menu [ESC]");
  ui.button(renderer, {1070, 506, 160, 44}, "Log [B]");
}

void VNTextBox::drawStyledText(RendererRef renderer, UI &ui,
                               const std::string &text, float x, float y,
                               float wrap, const SDL_Color &color,
                               float fontSize) {
  const auto draw = [&](float dx, float dy, const SDL_Color &tint) {
    if (fontSize > 0.0f)
      ui.textSized(renderer, text, x + dx, y + dy, wrap, fontSize, tint);
    else
      ui.text(renderer, text, x + dx, y + dy, wrap, tint);
  };
  if (textOutlineWidth > 0 && textOutlineColor.a > 0) {
    const float d = static_cast<float>(textOutlineWidth);
    // Eight neighbours give a smooth outline on both horizontal and diagonal
    // strokes without needing a font with an outline API.
    draw(-d, 0, textOutlineColor);
    draw(d, 0, textOutlineColor);
    draw(0, -d, textOutlineColor);
    draw(0, d, textOutlineColor);
    draw(-d, -d, textOutlineColor);
    draw(d, -d, textOutlineColor);
    draw(-d, d, textOutlineColor);
    draw(d, d, textOutlineColor);
  }
  draw(0, 0, color);
}

void VNTextBox::renderSpeaker(RendererRef renderer) {
  auto &ui = engine.GetVNUI();
  const auto color = engine.GetSettings().vnTextColorOverride
                         ? engine.GetSettings().vnSpeakerColor
                         : speakerColor;
  drawStyledText(renderer, ui, speakerName, 60, 510, 810, color,
                 speakerFontSize);
}

void VNTextBox::renderDialogue(RendererRef renderer) {
  SDL_Rect clip{60, 550, 1140, 130};
  SDL_SetRenderClipRect(&renderer, &clip);

  auto &ui = engine.GetVNUI();
  constexpr float textWidth = 1140.0f;
  if (!layoutValid_ || layoutSource_ != fullDialogue ||
      layoutDensity_ != ui.pixelDensity() || layoutUiId_ != ui.instanceId()) {
    const auto measureText = [&ui](const std::string &str) -> float {
      return ui.textWidth(str);
    };
    dialogueLayout_ =
        TextWrapping::layout(fullDialogue, measureText, textWidth);
    layoutSource_ = fullDialogue;
    layoutDensity_ = ui.pixelDensity();
    layoutUiId_ = ui.instanceId();
    layoutValid_ = true;
  }

  // Revealing only slices the precomputed source-to-display mapping.
  const auto lines =
      TextWrapping::visibleLines(dialogueLayout_, visibleDialogue.size());

  const auto color = engine.GetSettings().vnTextColorOverride
                         ? engine.GetSettings().vnTextColor
                         : dialogueColor;
  // Outlined text needs a slightly taller line stride to avoid the shadow of
  // one line touching the next.
  const int stride = dialogueFontSize > 26.0f ? 32 : 28;
  int yOffset = 550;
  for (const auto &line : lines) {
    drawStyledText(renderer, ui, line, 60, static_cast<float>(yOffset), 1140.0f,
                   color, dialogueFontSize);
    yOffset += stride;
  }

  SDL_SetRenderClipRect(&renderer, nullptr);
}

void VNTextBox::renderIndicator(RendererRef renderer) {
  engine.GetUI().text(renderer, "v", 1200, 665, 25, {255, 215, 100, 255});
}

VNTextBox &GetVNTextBoxRuntime(Engine &engineRef, const std::string &speaker,
                               const std::string &dialogue, double cps) {
  auto &box = engineRef.sharedScene<VNTextBox>(speaker, dialogue, cps);
  return box;
}

} // namespace Kotonoha
