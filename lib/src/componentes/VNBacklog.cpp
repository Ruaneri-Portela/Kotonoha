#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/UI.hpp>
#include <algorithm>

namespace Kotonoha {

VNBacklog::VNBacklog(Engine &engineRef) : Scene(engineRef) {}

void VNBacklog::addEntry(const std::string &speaker, const std::string &text) {
  if (text.empty())
    return;
  entries.push_back({speaker, text});
  scrollToBottom = true;
}

void VNBacklog::clear() {
  entries.clear();
  scroll = maxScroll = 0;
}

void VNBacklog::onEvent(EventRef event) {
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_B)
      toggle();
    else if (isOpen && event.key.key == SDLK_ESCAPE)
      close();
    else if (isOpen && event.key.key == SDLK_UP)
      scroll = std::max(0.0f, scroll - 80);
    else if (isOpen && event.key.key == SDLK_DOWN)
      scroll = std::min(maxScroll, scroll + 80);
  }
  if (isOpen && event.type == SDL_EVENT_MOUSE_WHEEL) {
    scrollToBottom = false;
    const float direction =
        event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
    scroll =
        std::clamp(scroll - event.wheel.y * direction * 60, 0.0f, maxScroll);
  }
  if (isOpen && UI::clicked(event, {980, 100, 160, 44}))
    close();
}

void VNBacklog::update() {}

void VNBacklog::render(RendererRef renderer, float) {
  if (!isVisible())
    return;
  renderFrame(renderer);
  renderEntries(renderer);
}

void VNBacklog::renderFrame(RendererRef renderer) {
  auto &ui = engine.GetUI();
  ui.panel(renderer, {0, 0, UI::width, UI::height}, {0, 0, 0, 160});
  ui.panel(renderer, {120, 80, 1040, 560});
  ui.text(renderer, "Dialogue history", 145, 105, 800,
          {255, 215, 100, 255});
  ui.button(renderer, {980, 100, 160, 44}, "Close [ESC]");
}

void VNBacklog::renderEntries(RendererRef renderer) {
  auto &ui = engine.GetUI();
  float total = 0;
  for (const auto &entry : entries)
    total += ui.textHeight(renderer, entry.speaker, 960) +
             ui.textHeight(renderer, entry.text, 960) + 24;
  maxScroll = std::max(0.0f, total - 450);
  scroll = scrollToBottom ? maxScroll : std::clamp(scroll, 0.0f, maxScroll);
  scrollToBottom = false;
  SDL_Rect clip{145, 165, 990, 450};
  SDL_SetRenderClipRect(&renderer, &clip);
  float y = 165 - scroll;
  if (entries.empty())
    ui.text(renderer, "No dialogue yet.", 145, y, 960);
  for (const auto &entry : entries) {
    y += ui.text(renderer, entry.speaker, 145, y, 960, {255, 215, 100, 255});
    y += ui.text(renderer, entry.text, 145, y, 960) + 24;
  }
  SDL_SetRenderClipRect(&renderer, nullptr);
}

VNBacklog &GetVNBacklogRuntime(Engine &engineRef) {
  static VNBacklog backlog{engineRef};
  return backlog;
}

} // namespace Kotonoha
