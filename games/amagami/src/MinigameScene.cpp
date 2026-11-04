#include <Amagami/MinigameScene.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Amagami {

void AmagamiMinigameScene::start(Type type, double duration,
                                 ResultCallback callback) {
  if (!std::isfinite(duration) || duration <= 0)
    throw std::invalid_argument(
        "Minigame duration must be positive and finite");
  clock_.reset();
  type_ = type;
  duration_ = duration;
  callback_ = callback;
  elapsed_ = 0;
  active_ = true;
  input_processed_ = false;
  bar_pos_ = 0;
  target_pos_ = 0.5f + (float)(rand() % 40) / 100.0f; // Random target position
  target_width_ = 0.15f;
}

void AmagamiMinigameScene::update() {
  const auto delta = clock_.seconds();
  clock_.reset();
  if (!engine_.isAgentPaused())
    update(delta);
}
void AmagamiMinigameScene::update(double delta) {
  if (!std::isfinite(delta) || delta < 0)
    throw std::invalid_argument(
        "Minigame delta must be finite and nonnegative");
  if (!active_)
    return;

  elapsed_ += delta;
  if (elapsed_ >= duration_) {
    active_ = false;
    if (callback_) {
      callback_({false, 0.0f});
    }
    return;
  }

  // Move the bar automatically to simulate a moving target
  // This is a simple implementation of a "timing bar" minigame
  // where the player must hit the key when the bar is in the target area.
  bar_pos_ += (float)(delta * 0.5); // Speed of movement
  if (bar_pos_ > 1.0f)
    bar_pos_ = 0.0f;
}

void AmagamiMinigameScene::render(Kotonoha::RendererRef r, float scale) {
  if (!active_)
    return;

  auto &ui = engine_.GetUI();
  // Render background overlay
  ui.panel(r, {0, 0, 640, 448}, {0, 0, 0, 180});

  // Render the bar track
  SDL_FRect track_rect{120, 224, 400, 20};
  ui.panel(r, track_rect, {50, 50, 50, 255});

  // Render the target area
  SDL_FRect target_rect{120 + (target_pos_ - target_width_ / 2) * 400, 224,
                        target_width_ * 400, 20};
  ui.panel(r, target_rect, {0, 255, 0, 150});

  // Render the moving bar
  SDL_FRect bar_rect{120 + bar_pos_ * 400, 224, 10, 20};
  ui.panel(r, bar_rect, {255, 255, 255, 255});

  // Render instruction
  ui.textSized(r, "PRESS SPACE!", 20, 100, 320, 30, {255, 255, 255, 255});
}

bool AmagamiMinigameScene::handleEvent(Kotonoha::EventRef event) {
  if (!active_ || input_processed_)
    return false;

  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
      event.key.key == SDLK_SPACE) {
    input_processed_ = true;

    // Check if bar is within target
    float dist = std::abs(bar_pos_ - target_pos_);
    bool success = dist <= (target_width_ / 2.0f);
    float score = success ? (1.0f - dist / (target_width_ / 2.0f)) : 0.0f;

    active_ = false;
    if (callback_) {
      callback_({success, score});
    }
    return true;
  }

  return false;
}

} // namespace Amagami
