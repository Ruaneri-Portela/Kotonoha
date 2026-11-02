#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/UI.hpp>

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

void VNTextBox::onEvent(EventRef) {}

bool VNTextBox::handleEvent(EventRef event) {
  if (!visible)
    return false;
  if (UI::clicked(event, {900, 506, 160, 44})) {
    openMenu();
    return true;
  }
  if (UI::clicked(event, {1070, 506, 160, 44})) {
    openBacklog();
    return true;
  }
  return false;
}

void VNTextBox::update() {
  const double delta = clock.seconds();
  clock.reset();
  if (!visible || isComplete || engine.isStoryPaused())
    return;
  const std::size_t totalChars = VNTextBoxUtility::CountUtf8Chars(fullDialogue);
  visibleChars = std::min(visibleChars + charactersPerSecond() * delta,
                          static_cast<double>(totalChars));
  const auto bytes =
      utf8Prefix(fullDialogue, static_cast<std::size_t>(visibleChars));
  visibleDialogue = fullDialogue.substr(0, bytes);
  isComplete = (visibleChars >= totalChars);
}

void VNTextBox::render(RendererRef renderer, float) {
  if (!visible)
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
  ui.button(renderer, {900, 506, 160, 44}, "Menu [ESC]");
  ui.button(renderer, {1070, 506, 160, 44}, "Log [B]");
}

void VNTextBox::renderSpeaker(RendererRef renderer) {
  engine.GetUI().text(renderer, speakerName, 60, 510, 810,
                      {255, 215, 100, 255});
}

void VNTextBox::renderDialogue(RendererRef renderer) {
  SDL_Rect clip{60, 550, 1140, 130};
  SDL_SetRenderClipRect(&renderer, &clip);

  // Utiliza textSize(str, width).x para obter a largura em pixels
  auto measureText = [this](const std::string &str) -> float {
    return engine.GetUI().textSize(str, 1140.0f).x;
  };

  const auto lines =
      VNTextBoxUtility::WrapText(visibleDialogue, measureText, 1140.0f);

  int yOffset = 550;
  for (const auto &line : lines) {
    engine.GetUI().text(renderer, line, 60, static_cast<float>(yOffset),
                        1140.0f);
    yOffset += 28;
  }

  SDL_SetRenderClipRect(&renderer, nullptr);
}

void VNTextBox::renderIndicator(RendererRef renderer) {
  engine.GetUI().text(renderer, "v", 1200, 665, 25, {255, 215, 100, 255});
}

VNTextBox &GetVNTextBoxRuntime(Engine &engineRef, const std::string &speaker,
                               const std::string &dialogue, double cps) {
  static VNTextBox box{engineRef, speaker, dialogue, cps};
  return box;
}

} // namespace Kotonoha
