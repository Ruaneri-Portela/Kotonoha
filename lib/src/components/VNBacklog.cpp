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
  invalidateLayout();
  scrollToBottom = true;
}

void VNBacklog::clear() {
  entries.clear();
  invalidateLayout();
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
  auto &ui = engine.GetVNUI();
  ui.backdrop(renderer, {0, 0, 0, 160});
  ui.panel(renderer, {120, 80, 1040, 560});
  const auto &settings = engine.GetSettings();
  const SDL_Color title = settings.vnTextColorOverride
                              ? settings.vnSpeakerColor
                              : SDL_Color{255, 215, 100, 255};
  ui.text(renderer, "Dialogue history", 145, 105, 800, title);
  ui.button(renderer, {980, 100, 160, 44}, "Close [ESC]");
}

void VNBacklog::renderEntries(RendererRef renderer) {
  auto &ui = engine.GetVNUI();
  const auto &settings = engine.GetSettings();
  const SDL_Color speaker = settings.vnTextColorOverride
                                ? settings.vnSpeakerColor
                                : SDL_Color{255, 215, 100, 255};
  const SDL_Color dialogue = settings.vnTextColorOverride
                                 ? settings.vnTextColor
                                 : SDL_Color{240, 240, 245, 255};
  constexpr float viewTop = 165.0f;
  constexpr float viewHeight = 450.0f;
  constexpr float viewBottom = viewTop + viewHeight;
  constexpr float textWidth = 960.0f;
  constexpr float spacing = 24.0f;

  // Layout is independent of scroll position. Measure once per entry and
  // rebuild only after content, font or raster scale changes.
  const float raster = UI::rasterDensity(renderer);
  if (entryOffsets_.size() != entries.size() + 1 ||
      layoutUiId_ != ui.instanceId() || layoutRaster_ != raster) {
    entryOffsets_.assign(entries.size() + 1, 0.0f);
    speakerHeights_.resize(entries.size());
    textHeights_.resize(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
      speakerHeights_[i] =
          ui.textHeight(renderer, entries[i].speaker, textWidth);
      textHeights_[i] = ui.textHeight(renderer, entries[i].text, textWidth);
      entryOffsets_[i + 1] =
          entryOffsets_[i] + speakerHeights_[i] + textHeights_[i] + spacing;
    }
    layoutUiId_ = ui.instanceId();
    layoutRaster_ = raster;
  }

  maxScroll = std::max(0.0f, entryOffsets_.back() - viewHeight);
  if (scrollToBottom) {
    targetScroll = scroll = maxScroll;
    scrollToBottom = false;
  } else {
    targetScroll = std::clamp(targetScroll, 0.0f, maxScroll);
    scroll = std::clamp(scroll, 0.0f, maxScroll);
  }

  const SDL_Rect clip{145, 165, 990, 450};
  SDL_SetRenderClipRect(&renderer, &clip);
  if (entries.empty())
    ui.text(renderer, "No dialogue yet.", 145, viewTop, textWidth, dialogue);

  // Binary search directly to the first potentially visible entry.
  const auto first =
      std::upper_bound(entryOffsets_.begin() + 1, entryOffsets_.end(), scroll);
  const auto begin =
      static_cast<std::size_t>(first - entryOffsets_.begin() - 1);
  for (std::size_t i = begin; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    const float y = viewTop + entryOffsets_[i] - scroll;
    if (y >= viewBottom)
      break;
    const float speakerHeight = speakerHeights_[i];
    const float textHeight = textHeights_[i];
    if (y + speakerHeight + textHeight + spacing <= viewTop)
      continue;
    if (y + speakerHeight > viewTop && y < viewBottom)
      ui.text(renderer, entry.speaker, 145, y, textWidth, speaker);
    const float textY = y + speakerHeight;
    if (textY + textHeight > viewTop && textY < viewBottom)
      ui.text(renderer, entry.text, 145, textY, textWidth, dialogue);
  }
  SDL_SetRenderClipRect(&renderer, nullptr);
}

VNBacklog &GetVNBacklogRuntime(Engine &engineRef) {
  auto &backlog = engineRef.sharedScene<VNBacklog>();
  return backlog;
}

} // namespace Kotonoha
