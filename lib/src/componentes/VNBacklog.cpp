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
  if (Kotonoha::BackAction(engine, event)) {
    close();
    return;
  }

  if (isOpen && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_B) {
    close();
    return;
  }

  if (isOpen && HandleBacklogScrollEvent(event, targetScroll, scrollVelocity, scrollToBottom, maxScroll)) {
    return;
  }

  if (isOpen && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
    if (UI::clicked(event, {980, 100, 160, 44})) {
      close();
      return;
    }
    isDragging = true;
    dragStartY = event.button.y;
    dragStartScroll = targetScroll;
    lastDragY = event.button.y;
    scrollVelocity = 0.0f;
    scrollToBottom = false;
  } else if (isOpen && event.type == SDL_EVENT_MOUSE_MOTION && isDragging) {
    const float currentY = event.motion.y;
    const float dy = currentY - dragStartY;
    targetScroll = std::clamp(dragStartScroll - dy, 0.0f, maxScroll);
    scroll = targetScroll;
    const float instVel = -(currentY - lastDragY);
    scrollVelocity = scrollVelocity * 0.4f + (instVel * 60.0f) * 0.6f;
    lastDragY = currentY;
  } else if (isOpen && event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
    isDragging = false;
  }

  if (isOpen && event.type == SDL_EVENT_FINGER_DOWN) {
    const float fingerY = event.tfinger.y * UI::height;
    isDragging = true;
    dragTouchId = event.tfinger.fingerID;
    dragStartY = fingerY;
    dragStartScroll = targetScroll;
    lastDragY = fingerY;
    scrollVelocity = 0.0f;
    scrollToBottom = false;
  } else if (isOpen && event.type == SDL_EVENT_FINGER_MOTION && isDragging && event.tfinger.fingerID == dragTouchId) {
    const float fingerY = event.tfinger.y * UI::height;
    const float dy = fingerY - dragStartY;
    targetScroll = std::clamp(dragStartScroll - dy, 0.0f, maxScroll);
    scroll = targetScroll;
    const float instVel = -(fingerY - lastDragY);
    scrollVelocity = scrollVelocity * 0.4f + (instVel * 60.0f) * 0.6f;
    lastDragY = fingerY;
  } else if (isOpen && event.type == SDL_EVENT_FINGER_UP && isDragging && event.tfinger.fingerID == dragTouchId) {
    isDragging = false;
  }
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
  ui.panel(renderer, {0, 0, UI::width, UI::height}, {0, 0, 0, 160});
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
