#include <Kotonoha/Gameplay.hpp>

namespace Kotonoha {

void Gameplay::UpdateCanvasSize(SDL_Window *window, SDL_Renderer *renderer) {
  int pixelW = 0, pixelH = 0;
  SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);

  windowWidth = pixelW;
  windowHeight = pixelH;

  SDL_FRect place{0, 0, 0, 0};
  const float renderWidth = static_cast<float>(windowWidth);
  const float renderHeight = static_cast<float>(windowHeight);
  const double windowAspectRatio = renderWidth / renderHeight;

  if (windowAspectRatio < aspectRatio) {
    place.w = renderWidth;
    place.h = renderWidth / aspectRatio;
    place.x = 0.0f;
    place.y = (renderHeight - place.h) * 0.5f;
  } else {
    place.h = renderHeight;
    place.w = renderHeight * aspectRatio;
    place.y = 0.0f;
    place.x = (renderWidth - place.w) * 0.5f;
  }

  drawCanvas->UpdateCanva(nullptr, -1, place);
}

Gameplay::Gameplay(const char *scriptPath, struct Kotonoha_Game *gameContext) {
  this->tm = Kotonoha_timeNew(true);
  this->sb = Kotonoha_TextRenderInit(this->tm, gameContext);

  this->video = new Video(this->tm);
  this->image = new Image(this->tm);
  this->audio = new Audio(static_cast<Sound *>(gameContext->sound), this->tm);
  this->drawCanvas = new Canvas();

  this->drawCanvas->RegisterCanva(
      this->image->Render, 0,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->image);
  this->drawCanvas->RegisterCanva(
      this->video->Render, 1,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->video);
  this->drawCanvas->RegisterCanva(
      Kotonoha_TextRenderDraw, 3,
      {0, 0, static_cast<float>(windowWidth), static_cast<float>(windowHeight)},
      this->sb);

  script = scriptPath;
  this->eventManager = new Event(scriptPath, this, gameContext);

#ifdef ANDROID
  playOnlyOnFocus = true;
  lastPauseStatus = gameContext->paused;
#endif
}

SDL_AppResult Gameplay::Main(struct Kotonoha_Game *gameContext) {
  if (firstFocus) {
    UpdateCanvasSize(gameContext->window, gameContext->render);
  }

  if (eventManager->CheckEnd(this)) {
    return SDL_APP_SUCCESS;
  }

  if (this->putPrompt) {
    drawCanvas->RegisterCanva(this->prompt->Render, 2,
                              {0, 0, static_cast<float>(windowWidth),
                               static_cast<float>(windowHeight)},
                              this->prompt);
    this->putPrompt = false;
  }

  if (!Kotonoha_timeIsStarted(tm))
    Kotonoha_timeStart(tm);

  if (gameContext->paused) {
    Pause();
  } else {
    Resume();
  }

  void *persistent = nullptr;
  do {
    SDL_Event event = Kotonoha_eventRead(&gameContext->eventQueu, &persistent);

    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
      switch (event.key.key) {
      case SDLK_P:
        TogglePause();
        gameContext->paused = Kotonoha_timeIsPaused(this->tm);
        break;

      case SDLK_I: {
        float timeInSeconds =
            static_cast<float>(Kotonoha_timeGet(this->tm)) / 1000.0f;
        SDL_Log("Time: %.2f, is paused %d\n", timeInSeconds,
                Kotonoha_timeIsPaused(this->tm));
        break;
      }

      case SDLK_S:
        Kotonoha_eventFree(&gameContext->eventQueu);
        return SDL_APP_SUCCESS;

      case SDLK_N:
        SeekForward(5000);
        break;

      case SDLK_B:
        SeekBackward(5000);
        break;

      case SDLK_Q:
        SeekForward(500);
        break;

      case SDLK_E:
        SeekBackward(500);
        break;

      case SDLK_R:
        Reset();
        break;

      case SDLK_SPACE:
        Resume();
        gameContext->paused = false;
        break;

      default:
        break;
      }
      break;

    case SDL_EVENT_WINDOW_RESIZED:
      UpdateCanvasSize(gameContext->window, gameContext->render);
      break;

#ifdef ANDROID
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      if (playOnlyOnFocus) {
        gameContext->paused = lastPauseStatus;
        if (!gameContext->paused) {
          Resume();
        }
      }
      break;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
      if (playOnlyOnFocus) {
        lastPauseStatus = gameContext->paused;
        gameContext->paused = true;
        Pause();
      }
      break;
#endif

    default:
      break;
    }

  } while (persistent != nullptr);

  SDL_AppResult result = drawCanvas->RenderCanvas(
      gameContext->window, gameContext->render, &gameContext->eventQueu);

  Kotonoha_eventFree(&gameContext->eventQueu);
  return result;
}

void Gameplay::Pause() { Kotonoha_timePause(this->tm); }

void Gameplay::Resume() { Kotonoha_timeResume(this->tm); }

void Gameplay::TogglePause() {
  if (Kotonoha_timeIsPaused(this->tm)) {
    Resume();
  } else {
    Pause();
  }
}

void Gameplay::Reset() {
  Kotonoha_timeReset(this->tm, true);
  reset = true;
}

void Gameplay::SeekForward(Uint64 ms) {
  Kotonoha_timeSeekForward(this->tm, ms);
}

void Gameplay::SeekBackward(Uint64 ms) {
  Kotonoha_timeSeekBackward(this->tm, ms);
  reset = true;
}

float Gameplay::GetLastTime() {
    return (float)this->eventManager->lastTime / 1000;
}

float Gameplay::GetTime() {
    return (float)Kotonoha_timeGet(this->tm)/ 1000;
}

void Gameplay::SetTime(float time) {
    Kotonoha_timeSet(this->tm, (Uint64)(time * 1000));
}

Gameplay::~Gameplay() {
  delete this->eventManager;
  delete this->drawCanvas;
  delete this->video;
  delete this->image;
  delete this->audio;

  if (prompt) {
    delete prompt;
  }

  Kotonoha_TextRenderShutdown(&(this->sb));
  Kotonoha_timeDestroy(this->tm);
}

} // namespace Kotonoha