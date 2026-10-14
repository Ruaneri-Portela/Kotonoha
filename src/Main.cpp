#include <Kotonoha/Init.hpp>
extern "C" {
#define SDL_MAIN_USE_CALLBACKS 1
#include <Kotonoha/speciais/Upscaling.h>
#include <SDL3/SDL_main.h>

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  SDL_AppResult status = SDL_APP_FAILURE;

  if (appstate == nullptr) {
    return status;
  }
  *appstate = nullptr;

  std::memset(&global, 0, sizeof(global));
  global.config = KotonohaConfigLoad(NULL, argc, argv);

  if (global.config.cancelOpen) {
    return SDL_APP_SUCCESS;
  }

  if (!Kotonoha::InitSDLSubsystems()) {
    return SDL_APP_FAILURE;
  }

  if (!Kotonoha::InitWindow() || !Kotonoha::InitRenderer() ||
      !Kotonoha::InitSound()) {
    TTF_Quit();
    SDL_Quit();
    return SDL_APP_FAILURE;
  }

  Kotonoha::Kotonoha *engine =
      new (std::nothrow) Kotonoha::Kotonoha(&status, global);

  if (engine == nullptr) {
    SDL_LogError(0, "Couldn't create Kotonoha engine instance");
    return SDL_APP_FAILURE;
  }

  if (status != SDL_APP_CONTINUE) {
    delete engine;
    return status;
  }

  std::vector<Uint8> imguiFontData;

  if (!Kotonoha::InitLibAssAndTTFs("NotoSans-Regular", engine->fonts,
                                   imguiFontData)) {
    delete engine;
    Kotonoha::CleanupGlobalResources();
    TTF_Quit();
    SDL_Quit();
    return SDL_APP_FAILURE;
  }

  if (status != SDL_APP_CONTINUE) {
    delete engine;
    Kotonoha::CleanupGlobalResources();
    TTF_Quit();
    SDL_Quit();
    return status;
  }

  for (int i = 0; i < global.config.scriptCount; ++i) {
    if (global.config.scriptsForced[i] != nullptr) {
      engine->LoadScriptFile(global.config.scriptsForced[i]);
    }
  }

  *appstate = engine;
  if (global.render != nullptr) {
    KotonohaBasicGuiInit(global, imguiFontData);
    SDL_ShowWindow(global.window);
  }

  KotonohaUpscalingInitInit(global.render, "Auto", global.config.scalerFactor);

  return status;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  if (event == nullptr) {
    return SDL_APP_CONTINUE;
  }

  KotonohaBasicGuiEvent(event);

  Kotonoha::Kotonoha *app = Kotonoha::GetApp(appstate);
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }
  if (app == nullptr) {
    return SDL_APP_CONTINUE;
  }

  return app->Event(event);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  Kotonoha::Kotonoha *app = Kotonoha::GetApp(appstate);
  if (app == nullptr || global.render == nullptr) {
    return SDL_APP_FAILURE;
  }

  Kotonoha::Gameplay *inRunning = nullptr;
  SDL_SetRenderDrawColor(global.render, 0, 0, 0, 0);
  SDL_RenderClear(global.render);

  const SDL_AppResult status = app->Main(&inRunning);
  if (status != SDL_APP_CONTINUE) {
    return status;
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
  if (!KotonohaBasicGuiRun(app, inRunning, global)) {
    return SDL_APP_SUCCESS;
  }
  SDL_RenderPresent(global.render);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;
  delete Kotonoha::GetApp(appstate);

  KotonohaUpscalingInitShutdown();
  Kotonoha::CleanupGlobalResources();
  KotonohaConfigFree(&global.config);

  TTF_Quit();
  SDL_Quit();
}

} // extern "C"