#include <Kotonoha/componentes/FadeScene.hpp>
#include <Kotonoha/core/UI.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

void FadeScene::add(const Event &event) {
  if (!std::isfinite(event.start) || !std::isfinite(event.end) || event.end < event.start)
    throw std::invalid_argument("Fade events need a finite, ordered time range");
  events.push_back(event);
}

void FadeScene::clear() {
  events.clear();
  selfClock = false;
}

void FadeScene::setPosition(double seconds) {
  if (!std::isfinite(seconds))
    throw std::invalid_argument("Fade position must be finite");
  selfClock = false;
  timelinePosition = seconds;
}

double FadeScene::position() const {
  return selfClock ? (SDL_GetTicksNS() - clockStart) / 1e9 : timelinePosition;
}

void FadeScene::play(Direction direction, double seconds, SDL_Color color, bool hold) {
  if (!std::isfinite(seconds) || seconds < 0)
    throw std::invalid_argument("Fade duration must be finite and non-negative");
  events.clear();
  selfClock = true;
  clockStart = SDL_GetTicksNS();
  events.push_back({0, seconds, color, direction, hold});
}

float FadeScene::alphaAt(const Event &event, double at) {
  const double duration = event.end - event.start;
  const double progress =
      duration <= 0 ? 1 : std::clamp((at - event.start) / duration, 0.0, 1.0);
  return static_cast<float>(event.direction == Direction::In ? 1 - progress : progress);
}

const FadeScene::Event *FadeScene::current() const {
  const double at = position();
  const Event *best = nullptr;
  for (const auto &event : events) {
    const bool inside = at >= event.start && at <= event.end;
    const bool held = event.hold && event.direction == Direction::Out && at > event.end;
    if ((inside || held) && (!best || event.start >= best->start))
      best = &event;
  }
  return best;
}

float FadeScene::alpha() const {
  const auto *event = current();
  return event ? alphaAt(*event, position()) : 0;
}

SDL_Color FadeScene::color() const {
  const auto *event = current();
  return event ? event->color : SDL_Color{0, 0, 0, 0};
}

bool FadeScene::active() const {
  const double at = position();
  return std::any_of(events.begin(), events.end(), [&](const Event &event) {
    return at >= event.start && at < event.end;
  });
}

void FadeScene::render(RendererRef renderer, float) {
  const auto *event = current();
  if (!event)
    return;
  const float coverage = alphaAt(*event, position()) * (event->color.a / 255.0f);
  if (coverage <= 0)
    return;
  SDL_Rect viewport{};
  SDL_GetRenderViewport(&renderer, &viewport);
  const SDL_FRect area = design ? SDL_FRect{0, 0, float(UI::width), float(UI::height)}
                                : SDL_FRect{0, 0, float(viewport.w), float(viewport.h)};
  SDL_BlendMode previous{};
  SDL_GetRenderDrawBlendMode(&renderer, &previous);
  SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(&renderer, event->color.r, event->color.g, event->color.b,
                         Uint8(std::lround(coverage * 255)));
  SDL_RenderFillRect(&renderer, &area);
  SDL_SetRenderDrawBlendMode(&renderer, previous);
}

} // namespace Kotonoha
