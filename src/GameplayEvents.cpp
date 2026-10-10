#include <Kotonoha/Gameplay.hpp>

extern bool Kotonoha_BasicGuiAnyFocus;

namespace Kotonoha {
    namespace {

    constexpr Uint64 kSeekBigMs = 5000;
    constexpr Uint64 kSeekSmallMs = 50;

    constexpr Sint16 kTriggerDeadZone = 3000;

    // 10 segundos de mídia por segundo, com o gatilho totalmente pressionado.
    constexpr float kMaxSeekMsPerSecond = 20000.0f;

    } // namespace

    void Gameplay::Events(SDL_Event* event, Kotonoha_Game* gameContext) {
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

    void Gameplay::HandleGamepadButton(
        SDL_Event* event,
        Kotonoha_Game* gameContext
    ) {
        if (Kotonoha_BasicGuiAnyFocus) {
            return;
        }

        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            gameContext->next = true;
            break;

        case SDL_GAMEPAD_BUTTON_EAST:
            gameContext->back = true;
            break;

        case SDL_GAMEPAD_BUTTON_WEST:
            Reset(true);
            break;

        case SDL_GAMEPAD_BUTTON_NORTH:
            gameContext->paused = !gameContext->paused;
            break;

        default:
            break;
        }
    }

    void Gameplay::HandleGamepadAxis(
        SDL_Event* event,
        Kotonoha_Game* gameContext
    ) {
        (void)gameContext;
        if (Kotonoha_BasicGuiAnyFocus) {
            return;
        }

        const SDL_GamepadAxis axis =
            static_cast<SDL_GamepadAxis>(event->gaxis.axis);

        const Sint16 value = event->gaxis.value;

        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
            SetLeftTrigger(value);
        }
        else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
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

    void Gameplay::HandleTriggerSeek(
        TriggerSeekState& state,
        float deltaTimeSeconds,
        bool backward
    ) {
        if (!state.active) {
            return;
        }

        const float force =
            static_cast<float>(state.value) /
            static_cast<float>(SDL_JOYSTICK_AXIS_MAX);

        const float seekMs =
            force *
            kMaxSeekMsPerSecond *
            deltaTimeSeconds;

        if (seekMs < 0.5f) {
            return;
        }

        const Uint64 seekAmount = static_cast<Uint64>(seekMs);

        if (backward) {
            SeekBackward(seekAmount);
        }
        else {
            SeekForward(seekAmount);
        }
    }

    void Gameplay::HandleKeyboard(
        SDL_Event* event,
        Kotonoha_Game* gameContext
    ) {
        if (Kotonoha_BasicGuiAnyFocus) {
            return;
        }

        switch (event->key.key) {
        case SDLK_P:
            gameContext->paused = !gameContext->paused;
            break;

        case SDLK_I: {
            const float timeInSeconds = GetTime();
            SDL_Log(
                "Time: %.2f, is paused %d\n",
                timeInSeconds,
                Kotonoha_timeIsPaused(this->tm)
            );
            break;
        }

        case SDLK_S:
            gameContext->next = true;
            break;

        case SDLK_D:
            gameContext->back = true;
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

    void Gameplay::SeekByKeyboardN(Kotonoha_Game* gameContext) {
        const Uint64 currentTime = Kotonoha_timeGet(this->tm);

        if (currentTime < eventManager->lastTime - kSeekSmallMs) {
            SeekForward(kSeekBigMs);
            return;
        }

        gameContext->next = true;
    }

    void Gameplay::SeekByKeyboardB(Kotonoha_Game* gameContext) {
        const Uint64 currentTime = Kotonoha_timeGet(this->tm);

        if (currentTime > kSeekSmallMs) {
            SeekBackward(kSeekBigMs);
            return;
        }

        gameContext->back = true;
    }


    void Gameplay::HandleWindowEvent(
        SDL_Event* event,
        Kotonoha_Game* gameContext
    ) {
        (void)event;

        UpdateCanvasSize(gameContext->window, gameContext->render);

    #ifdef Kotonoha_MobileSetup
        HandleMobileFocus(event, gameContext);
    #endif
    }

    #ifdef Kotonoha_MobileSetup
    void Gameplay::HandleMobileFocus(
        SDL_Event* event,
        Kotonoha_Game* gameContext
    ) {
        if (!playOnlyOnFocus) {
            return;
        }

        switch (event->type) {
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (loadingPauseActive) {
                pausedBeforeLoading = lastPauseStatus;
            }
            else {
                gameContext->paused = lastPauseStatus;
            }

            if (!loadingPauseActive && !gameContext->paused) {
                Resume();
            }
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            lastPauseStatus = loadingPauseActive
                ? pausedBeforeLoading
                : gameContext->paused;

            gameContext->paused = true;
            Pause();
            break;

        default:
            break;
        }
    }
    #endif
} // namespace Kotonoha