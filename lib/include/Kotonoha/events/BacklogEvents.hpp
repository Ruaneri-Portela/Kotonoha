#pragma once

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <algorithm>
#include <cmath>

namespace Kotonoha {

inline bool ShouldOpenBacklog(Engine &engine, EventRef event) {
  if (engine.GetRuntime().GetState() != GameState::Gameplay)
    return false;

  // Key B
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
      event.key.key == SDLK_B) {
    return true;
  }

  // Mouse wheel scroll up
  if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y > 0) {
    return true;
  }

  // Touch gesture: swipe from bottom to top
  static bool fingerActive = false;
  static SDL_FingerID activeFingerID = 0;
  static float fingerStartX = 0.0f;
  static float fingerStartY = 0.0f;

  if (event.type == SDL_EVENT_FINGER_DOWN) {
    fingerActive = true;
    activeFingerID = event.tfinger.fingerID;
    fingerStartX = event.tfinger.x;
    fingerStartY = event.tfinger.y;
  } else if (event.type == SDL_EVENT_FINGER_MOTION && fingerActive &&
             event.tfinger.fingerID == activeFingerID) {
    const float dy = event.tfinger.y - fingerStartY;
    const float dx = event.tfinger.x - fingerStartX;
    // Exige arrasto de no mínimo ~12% da tela para cima (antes era 5%)
    if (dy < -0.12f && std::abs(dy) > std::abs(dx) * 1.5f) {
      fingerActive = false;
      return true;
    }
  } else if (event.type == SDL_EVENT_FINGER_UP) {
    if (event.tfinger.fingerID == activeFingerID) {
      fingerActive = false;
    }
  }

  // Mouse drag: swipe from bottom to top
  static bool mouseActive = false;
  static float mouseStartX = 0.0f;
  static float mouseStartY = 0.0f;

  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    mouseActive = true;
    mouseStartX = event.button.x;
    mouseStartY = event.button.y;
  } else if (event.type == SDL_EVENT_MOUSE_MOTION && mouseActive) {
    const float dy = event.motion.y - mouseStartY;
    const float dx = event.motion.x - mouseStartX;
    // Exige 80px de movimento vertical (antes era 40px)
    if (dy < -80.0f && std::abs(dy) > std::abs(dx) * 1.5f) {
      mouseActive = false;
      return true;
    }
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
             event.button.button == SDL_BUTTON_LEFT) {
    mouseActive = false;
  }

  return false;
}

inline bool HandleBacklogScrollEvent(EventRef event, float &targetScroll,
                                     float &scrollVelocity,
                                     bool &scrollToBottom, float maxScroll) {
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_UP) {
      scrollToBottom = false;
      targetScroll = std::clamp(targetScroll - 50.0f, 0.0f, maxScroll);
      return true;
    } else if (event.key.key == SDLK_DOWN) {
      scrollToBottom = false;
      targetScroll = std::clamp(targetScroll + 50.0f, 0.0f, maxScroll);
      return true;
    } else if (event.key.key == SDLK_PAGEUP) {
      scrollToBottom = false;
      targetScroll = std::clamp(targetScroll - 300.0f, 0.0f, maxScroll);
      return true;
    } else if (event.key.key == SDLK_PAGEDOWN) {
      scrollToBottom = false;
      targetScroll = std::clamp(targetScroll + 300.0f, 0.0f, maxScroll);
      return true;
    }
  }

  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    scrollToBottom = false;
    const float direction =
        event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;

    // Passo direto de scroll reduzido (60 -> 30)
    const float wheelDelta = event.wheel.y * direction * 30.0f;
    targetScroll = std::clamp(targetScroll - wheelDelta, 0.0f, maxScroll);

    // Impulso de velocidade inercial reduzido (8 -> 3)
    scrollVelocity -= wheelDelta * 3.0f;
    return true;
  }

  return false;
}

} // namespace Kotonoha