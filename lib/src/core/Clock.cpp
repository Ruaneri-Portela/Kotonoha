#include <Kotonoha/core/Clock.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

namespace {

void validateDuration(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) {
    throw std::invalid_argument(
        "Clock: duration must be finite and non-negative");
  }
}

} // namespace

Clock::Clock() : anchor(SDL_GetTicks()) {}

void Clock::pause() {
  if (paused)
    return;

  const Uint64 now = SDL_GetTicks();

  position += static_cast<double>(now - anchor) / 1000.0;
  anchor = now;
  paused = true;
}

void Clock::resume() {
  if (!paused)
    return;

  anchor = SDL_GetTicks();
  paused = false;
}

void Clock::reset() {
  position = 0.0;
  anchor = SDL_GetTicks();
}

void Clock::advance(double seconds) {
  validateDuration(seconds);

  const Uint64 now = SDL_GetTicks();

  if (!paused)
    position += static_cast<double>(now - anchor) / 1000.0;

  position += seconds;
  anchor = now;
}

void Clock::rewind(double seconds) {
  validateDuration(seconds);

  const Uint64 now = SDL_GetTicks();

  if (!paused)
    position += static_cast<double>(now - anchor) / 1000.0;

  position = std::max(0.0, position - seconds);
  anchor = now;
}

double Clock::seconds() const {
  if (paused)
    return position;

  const Uint64 now = SDL_GetTicks();

  return position + static_cast<double>(now - anchor) / 1000.0;
}

double Clock::milliseconds() const { return seconds() * 1000.0; }

bool Clock::isPaused() const { return paused; }

} // namespace Kotonoha