#define SDL_MAIN_USE_CALLBACKS 1
#include <Kotonoha/Init.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/debug/LogPanel.hpp>
#include <Kotonoha/specials/Upscaling.h>
#include <Kotonoha/utils/Log.h>
#include <SDL3/SDL_main.h>
#include <cstring>
#include <imgui.h>

extern "C" {
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  SDL_AppResult status = SDL_APP_FAILURE;

  if (appstate == nullptr) {
    return status;
  }
  *appstate = nullptr;

  KotonohaLogInit();
  KLOG_INFO(CORE, "Kotonoha starting");
  Kotonoha::Debug::InstallLogCapture();
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  std::memset(&global, 0, sizeof(global));

  if (!KotonohaConfigLoad(&global.config, "assets/Kotonoha.ini", argc, argv) ||
      global.config.cancelOpen) {
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
    KLOG_ERROR(CORE, "Couldn't create Kotonoha engine instance");
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
    Kotonoha::Debug::Init(global, imguiFontData);
    SDL_ShowWindow(global.window);
  }

  KotonohaUpscalingInit(global.render, "Auto", &global.config);

  return status;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  if (event == nullptr) {
    return SDL_APP_CONTINUE;
  }

  Kotonoha::Debug::ProcessEvent(event);

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
  SDL_SetRenderDrawColor(global.render, 0, 0, 0, 255);
  SDL_RenderClear(global.render);

  if (app->IsSplashActive()) {
    app->RenderSplash();
    SDL_RenderPresent(global.render);
    return SDL_APP_CONTINUE;
  }

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

  if (!Kotonoha::Debug::Run(app, inRunning, global)) {
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
  Kotonoha::Debug::UninstallLogCapture();

  TTF_Quit();
  SDL_Quit();
}

} // extern "C"