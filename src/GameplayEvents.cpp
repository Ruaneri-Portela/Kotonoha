#include <Kotonoha/utils/Log.h>
#include <Kotonoha/Gameplay.hpp>

#include <Kotonoha/debug/DebugUi.hpp>

namespace Kotonoha {
namespace {

constexpr Uint64 kSeekBigMs = 5000;
constexpr Uint64 kSeekSmallMs = 50;

constexpr Sint16 kTriggerDeadZone = 3000;

// 10 segundos de mídia por segundo, com o gatilho totalmente pressionado.
constexpr float kMaxSeekMsPerSecond = 20000.0f;

} // namespace

void Gameplay::Events(SDL_Event *event, Kotonoha_Game *gameContext) {
  if (event == nullptr) {
    return;
  }

  const bool promptActive =
      this->prompt != nullptr && this->prompt->IsActive();
  if (this->prompt != nullptr && !Debug::IsAnyWindowFocused()) {
    this->prompt->HandleEvent(event, gameContext, this->audio);
  }

  switch (event->type) {
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    HandleGamepadButton(event, gameContext);
    break;

  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    HandleGamepadAxis(event, gameContext);
    break;

  case SDL_EVENT_KEY_DOWN:
    HandleKeyboard(event, gameContext);
    break;

  case SDL_EVENT_WINDOW_RESIZED:
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
  case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    HandleWindowEvent(event, gameContext);
    break;

  default:
    break;
  }
}

void Gameplay::HandleGamepadButton(SDL_Event *event,
                                   Kotonoha_Game *gameContext) {
  if (Debug::IsAnyWindowFocused()) {
    return;
  }

  switch (event->gbutton.button) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    if (this->prompt == nullptr || !this->prompt->IsActive()) {
      gameContext->config.next = true;
    }
    break;

  case SDL_GAMEPAD_BUTTON_EAST:
    gameContext->config.back = true;
    break;

  case SDL_GAMEPAD_BUTTON_WEST:
    Reset(true);
    break;

  case SDL_GAMEPAD_BUTTON_NORTH:
    gameContext->config.paused = !gameContext->config.paused;
    break;

  default:
    break;
  }
}

void Gameplay::HandleGamepadAxis(SDL_Event *event, Kotonoha_Game *gameContext) {
  (void)gameContext;
  if (Debug::IsAnyWindowFocused()) {
    return;
  }

  const SDL_GamepadAxis axis = static_cast<SDL_GamepadAxis>(event->gaxis.axis);

  const Sint16 value = event->gaxis.value;

  if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
    SetLeftTrigger(value);
  } else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
    SetRightTrigger(value);
  }
}

void Gameplay::SetLeftTrigger(Sint16 value) {
  leftTrigger.value = value;
  leftTrigger.active = value >= kTriggerDeadZone;
}

void Gameplay::SetRightTrigger(Sint16 value) {
  rightTrigger.value = value;
  rightTrigger.active = value >= kTriggerDeadZone;
}

void Gameplay::UpdateTriggers(float deltaTimeSeconds) {
  HandleTriggerSeek(leftTrigger, deltaTimeSeconds, true);
  HandleTriggerSeek(rightTrigger, deltaTimeSeconds, false);
}

void Gameplay::HandleTriggerSeek(TriggerSeekState &state,
                                 float deltaTimeSeconds, bool backward) {
  if (!state.active) {
    return;
  }

  const float force = static_cast<float>(state.value) /
                      static_cast<float>(SDL_JOYSTICK_AXIS_MAX);

  const float seekMs = force * kMaxSeekMsPerSecond * deltaTimeSeconds;

  if (seekMs < 0.5f) {
    return;
  }

  const Uint64 seekAmount = static_cast<Uint64>(seekMs);

  if (backward) {
    SeekBackward(seekAmount);
  } else {
    SeekForward(seekAmount);
  }
}

void Gameplay::HandleKeyboard(SDL_Event *event, Kotonoha_Game *gameContext) {
  if (Debug::IsAnyWindowFocused()) {
    return;
  }

  switch (event->key.key) {
  case SDLK_P:
    gameContext->config.paused = !gameContext->config.paused;
    break;

  case SDLK_SPACE:
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    if (this->prompt == nullptr || !this->prompt->IsActive()) {
      gameContext->config.next = true;
    }
    break;

  case SDLK_I: {
    const float timeInSeconds = GetTime();
    KLOG_INFO(SCRIPT, "Time: %.2f, is paused %d", timeInSeconds,
            KotonohaTimeIsPaused(this->tm));
    break;
  }

  case SDLK_S:
    gameContext->config.next = true;
    break;

  case SDLK_D:
    gameContext->config.back = true;
    break;

  case SDLK_N:
    SeekByKeyboardN(gameContext);
    break;

  case SDLK_B:
    SeekByKeyboardB(gameContext);
    break;

  case SDLK_Q:
    SeekForward(kSeekSmallMs);
    break;

  case SDLK_E:
    SeekBackward(kSeekSmallMs);
    break;

  case SDLK_R:
    Reset(true);
    break;

  default:
    break;
  }
}

void Gameplay::SeekByKeyboardN(Kotonoha_Game *gameContext) {
  const Uint64 currentTime = KotonohaTimeGet(this->tm);

  if (currentTime < eventManager->lastTime - kSeekSmallMs) {
    SeekForward(kSeekBigMs);
    return;
  }

  gameContext->config.next = true;
}

void Gameplay::SeekByKeyboardB(Kotonoha_Game *gameContext) {
  const Uint64 currentTime = KotonohaTimeGet(this->tm);

  if (currentTime > kSeekSmallMs) {
    SeekBackward(kSeekBigMs);
    return;
  }

  gameContext->config.back = true;
}

void Gameplay::HandleWindowEvent(SDL_Event *event, Kotonoha_Game *gameContext) {
  (void)event;

  UpdateCanvasSize(gameContext->window, gameContext->render);

#if defined(KOTONOHA_MOBILE)
  HandleMobileFocus(event, gameContext);
#endif
}

#if defined(KOTONOHA_MOBILE)
void Gameplay::HandleMobileFocus(SDL_Event *event, Kotonoha_Game *gameContext) {
  if (!playOnlyOnFocus) {
    return;
  }

  switch (event->type) {
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    if (loadingPauseActive) {
      pausedBeforeLoading = lastPauseStatus;
    } else {
      gameContext->config.paused = lastPauseStatus;
    }

    if (!loadingPauseActive && !gameContext->config.paused) {
      Resume();
    }
    break;

  case SDL_EVENT_WINDOW_FOCUS_LOST:
    lastPauseStatus =
        loadingPauseActive ? pausedBeforeLoading : gameContext->config.paused;

    gameContext->config.paused = true;
    Pause();
    break;

  default:
    break;
  }
}
#endif
} // namespace Kotonoha