#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/BacklogEvents.hpp>
#include <algorithm>
#include <cmath>

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
  scroll = targetScroll = maxScroll = 0;
  isDragging = false;
  scrollVelocity = 0;
}

void VNBacklog::onEvent(EventRef event) {
  if (!isOpen)
    return;
  if (BackAction(engine, event) ||
      (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
       event.key.key == SDLK_B) ||
      UI::clicked(event, {980, 100, 160, 44})) {
    close();
    return;
  }
  scrollGesture.handle(event, UI::height, targetScroll, scrollVelocity,
                       scrollToBottom, maxScroll);
  isDragging = scrollGesture.isDragging();
  if (isDragging)
    scroll = targetScroll;
}

void VNBacklog::update() {
  if (!isOpen)
    return;
  const float dt = std::min(0.1f, static_cast<float>(clock.seconds()));
  clock.reset();

  if (isDragging) {
    scroll = std::clamp(targetScroll, 0.0f, maxScroll);
  } else {
    if (std::abs(scrollVelocity) > 1.0f) {
      targetScroll += scrollVelocity * dt;
      scrollVelocity *= std::pow(0.02f, dt);
      targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
      if (targetScroll <= 0.0f || targetScroll >= maxScroll) {
        scrollVelocity = 0.0f;
      }
    }
    if (dt > 0.0001f) {
      scroll += (targetScroll - scroll) * (1.0f - std::exp(-25.0f * dt));
      scroll = std::clamp(scroll, 0.0f, maxScroll);
    }
  }
}

void VNBacklog::render(RendererRef renderer, float) {
  if (!isVisible())
    return;
  renderFrame(renderer);
  renderEntries(renderer);
}

void VNBacklog::renderFrame(RendererRef renderer) {
  auto &ui = engine.GetUI();
  ui.backdrop(renderer, {0, 0, 0, 160});
  ui.panel(renderer, {120, 80, 1040, 560});
  ui.text(renderer, "Dialogue history", 145, 105, 800, {255, 215, 100, 255});
  ui.button(renderer, {980, 100, 160, 44}, "Close [ESC]");
}

void VNBacklog::renderEntries(RendererRef renderer) {
  auto &ui = engine.GetUI();
  float total = 0;
  for (const auto &entry : entries)
    total += ui.textHeight(renderer, entry.speaker, 960) +
             ui.textHeight(renderer, entry.text, 960) + 24;
  maxScroll = std::max(0.0f, total - 450);
  if (scrollToBottom) {
    targetScroll = scroll = maxScroll;
    scrollToBottom = false;
  } else {
    targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
    scroll = std::clamp(scroll, 0.0f, maxScroll);
  }
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
