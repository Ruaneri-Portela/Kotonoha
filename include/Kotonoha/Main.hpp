#pragma once

// Engine entry point.  A game's Main.cpp includes this header at its very end.
// It defines the SDL callbacks with the engine's default behaviour (engine
// master lifecycle, forced scripts, FPS/timestamp/loading overlays, debug UI)
// and then calls the game's Local* hooks, which extend that behaviour:
//
//   LocalArgs     inspect/consume game-specific argv before the engine parses it.
//   LocalInit     game setup after the engine exists (GetApp(*appstate)).
//   LocalEvent    per-event handling after the engine handled the event.
//   LocalIterate  per-frame work.  A game that renders the whole frame itself
//                 calls Kotonoha::MarkFrameHandled() to skip the default frame.
//   LocalQuit     game teardown, run before the engine is shut down.
//
// A game with no extras implements every hook as "return SDL_APP_CONTINUE" (or
// an empty body).  The reported SDL_AppResult is always the worst of the
// default and local results: FAILURE > SUCCESS > CONTINUE.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include <Kotonoha/Init.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/utils/Log.h>

SDL_AppResult LocalArgs(int argc, char *argv[]);
SDL_AppResult LocalInit(void **appstate, int argc, char *argv[]);
SDL_AppResult LocalEvent(void *appstate, SDL_Event *event);
SDL_AppResult LocalIterate(void *appstate);
void LocalQuit(void *appstate, SDL_AppResult result);

namespace Kotonoha {

// SDL_APP_CONTINUE < SDL_APP_SUCCESS < SDL_APP_FAILURE.
inline SDL_AppResult WorstResult(SDL_AppResult a, SDL_AppResult b) {
  return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
}

inline bool &FrameHandledFlag() {
  static bool handled = false;
  return handled;
}

inline void MarkFrameHandled() { FrameHandledFlag() = true; }

inline SDL_AppResult DefaultInit(void **appstate, int argc, char *argv[]) {
  if (appstate == nullptr) {
    return SDL_APP_FAILURE;
  }
  *appstate = nullptr;

  SDL_AppResult result = LocalArgs(argc, argv);
  if (result != SDL_APP_CONTINUE) {
    return result;
  }

  Kotonoha *engine = nullptr;
  result = EngineMasterInit(&engine, argc, argv);
  if (result != SDL_APP_CONTINUE) {
    return result;
  }
  // SDL calls SDL_AppQuit after a failed init, which releases the engine.
  *appstate = engine;

  for (int i = 0; i < global.config.scriptCount; ++i) {
    if (global.config.scriptsForced[i] != nullptr) {
      engine->LoadScriptFile(global.config.scriptsForced[i]);
    }
  }

  result = WorstResult(result, LocalInit(appstate, argc, argv));
  if (result == SDL_APP_CONTINUE) {
    SDL_ShowWindow(global.window);
  }
  return result;
}

inline SDL_AppResult DefaultEvent(void *appstate, SDL_Event *event) {
  SDL_AppResult result = SDL_APP_CONTINUE;
  if (event->type == SDL_EVENT_QUIT) {
    result = SDL_APP_SUCCESS;
  }
  if (Kotonoha *app = GetApp(appstate)) {
    result = WorstResult(result, EngineMasterEvent(app, event));
  }
  return WorstResult(result, LocalEvent(appstate, event));
}

inline SDL_AppResult DefaultFrame(Kotonoha *app) {
  Gameplay *inRunning = nullptr;
  SDL_SetRenderDrawColor(global.render, 0, 0, 0, 255);
  SDL_RenderClear(global.render);

  // While the splash is active EngineMasterMain draws and presents it.
  const SDL_AppResult status = EngineMasterMain(app, &inRunning);
  if (status != SDL_APP_CONTINUE) {
    return status;
  }
  if (app->IsSplashActive()) {
    return SDL_APP_CONTINUE;
  }

  app->CacheSceneTransitionFrameIfNeeded(inRunning);
  app->PresentSceneTransitionFrame(inRunning);

  TTF_Font *preferredFont = nullptr;
  auto it = app->fonts.find("ConcertOne-Regular");
  if (it != app->fonts.end()) {
    preferredFont = it->second;
  }

  if (global.config.showFps) {
    KotonohaFPSRender(global.window, global.render, preferredFont, nullptr);
  }
  if (global.config.showTimestamp && inRunning != nullptr) {
    void *timestampArgs[2] = {inRunning->tm, preferredFont};
    KotonohaTimestampRender(global.window, global.render, timestampArgs,
                            nullptr);
  }
  if (inRunning != nullptr && inRunning->ShouldShowLoadingOverlay()) {
    KotonohaLoadingRender(global.window, global.render, preferredFont, nullptr);
  }

  if (!Debug::Run(app, inRunning, global)) {
    return SDL_APP_SUCCESS;
  }
  SDL_RenderPresent(global.render);
  return SDL_APP_CONTINUE;
}

inline SDL_AppResult DefaultIterate(void *appstate) {
  Kotonoha *app = GetApp(appstate);
  if (app == nullptr || global.render == nullptr) {
    return SDL_APP_FAILURE;
  }

  FrameHandledFlag() = false;
  SDL_AppResult result = LocalIterate(appstate);
  if (FrameHandledFlag()) {
    return result;
  }
  return WorstResult(result, DefaultFrame(app));
}

inline void DefaultQuit(void *appstate, SDL_AppResult result) {
  LocalQuit(appstate, result);
  Kotonoha *engine = GetApp(appstate);
  EngineMasterShutdown(&engine);
}

} // namespace Kotonoha

extern "C" {

inline SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  return Kotonoha::DefaultInit(appstate, argc, argv);
}

inline SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  if (event == nullptr) {
    return SDL_APP_CONTINUE;
  }
  return Kotonoha::DefaultEvent(appstate, event);
}

inline SDL_AppResult SDL_AppIterate(void *appstate) {
  return Kotonoha::DefaultIterate(appstate);
}

inline void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  Kotonoha::DefaultQuit(appstate, result);
}

} // extern "C"
