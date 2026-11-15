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

void VNTextBox::renderSpeaker(RendererRef renderer) {
  engine.GetVNUI().text(renderer, speakerName, 60, 510, 810,
                        engine.GetSettings().vnTextColorOverride
                            ? engine.GetSettings().vnSpeakerColor
                            : SDL_Color{255, 215, 100, 255});
}

void VNTextBox::renderDialogue(RendererRef renderer) {
  SDL_Rect clip{60, 550, 1140, 130};
  SDL_SetRenderClipRect(&renderer, &clip);

  // Utiliza textSize(str, width).x para obter a largura em pixels
  auto measureText = [this](const std::string &str) -> float {
    return engine.GetVNUI().textSize(str, 1140.0f).x;
  };

  const auto lines = VNTextBoxUtility::RevealLines(
      fullDialogue, visibleDialogue.size(), measureText, 1140.0f);

  int yOffset = 550;
  for (const auto &line : lines) {
    engine.GetVNUI().text(renderer, line, 60, static_cast<float>(yOffset),
                          1140.0f,
                          engine.GetSettings().vnTextColorOverride
                              ? engine.GetSettings().vnTextColor
                              : SDL_Color{240, 240, 245, 255});
    yOffset += 28;
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
