#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/BacklogEvents.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
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

bool VNBacklog::onEvent(EventRef rawEvent) {
  if (!isOpen)
    return false;
  const auto event = MapControllerEvent(rawEvent, false, true);
  if (!touchActivation.accept(event))
    return true;
  if (BackAction(engine, event) ||
      (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
       event.key.key == SDLK_B) ||
      UI::clicked(event, closeButtonArea())) {
    close();
    return true;
  }
  if (scrollGesture.handle(event, scrollInputHeight(), targetScroll,
                           scrollVelocity, scrollToBottom, maxScroll)) {
    isDragging = scrollGesture.isDragging();
    if (isDragging)
      scroll = targetScroll;
    return true;
  }
  return false;
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

  constexpr float viewTop = 165.0f;
  constexpr float viewHeight = 450.0f;
  constexpr float viewBottom = viewTop + viewHeight;
  constexpr float textWidth = 960.0f;
  constexpr float spacing = 24.0f;

  // Calcula a altura total das entradas.
  float total = 0.0f;

  for (const auto &entry : entries) {
    total += ui.textHeight(renderer, entry.speaker, textWidth);
    total += ui.textHeight(renderer, entry.text, textWidth);
    total += spacing;
  }

  maxScroll = std::max(0.0f, total - viewHeight);

  if (scrollToBottom) {
    targetScroll = scroll = maxScroll;
    scrollToBottom = false;
  } else {
    targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
    scroll = std::clamp(scroll, 0.0f, maxScroll);
  }

  SDL_Rect clip{145, 165, 990, 450};
  SDL_SetRenderClipRect(&renderer, &clip);

  float y = viewTop - scroll;

  if (entries.empty()) {
    ui.text(renderer, "No dialogue yet.", 145, y, textWidth);
  }

  for (const auto &entry : entries) {
    const float speakerHeight =
        ui.textHeight(renderer, entry.speaker, textWidth);

    const float textHeight = ui.textHeight(renderer, entry.text, textWidth);

    const float entryHeight = speakerHeight + textHeight + spacing;

    // Entrada completamente acima da área visível.
    if (y + entryHeight <= viewTop) {
      y += entryHeight;
      continue;
    }

    // Entrada completamente abaixo da área visível.
    // Como as entradas estão ordenadas, podemos parar.
    if (y >= viewBottom)
      break;

    // Renderiza apenas os textos que intersectam a área visível.
    if (y + speakerHeight > viewTop && y < viewBottom) {
      ui.text(renderer, entry.speaker, 145, y, textWidth, {255, 215, 100, 255});
    }

    const float textY = y + speakerHeight;

    if (textY + textHeight > viewTop && textY < viewBottom) {
      ui.text(renderer, entry.text, 145, textY, textWidth);
    }

    y += entryHeight;
  }

  SDL_SetRenderClipRect(&renderer, nullptr);
}

VNBacklog &GetVNBacklogRuntime(Engine &engineRef) {
  auto &backlog = engineRef.sharedScene<VNBacklog>();
  return backlog;
}

} // namespace Kotonoha
