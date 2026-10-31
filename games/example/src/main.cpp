#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "DemoGame.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <cstdio>

#include <exception>
#include <stdexcept>

namespace {
struct Application {
  KotonohaExample::DemoGame game;
  Kotonoha::Engine engine;
  long frames = -1;
  explicit Application(Kotonoha::EngineOptions options)
      : engine(game, std::move(options)) {}
};
} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  *appstate = nullptr;
  try {
    Kotonoha::EngineOptions options;
    options.persistSettings = true;
    Kotonoha::CommandLine arguments("Kotonoha", options);
    arguments.parse(argc, argv);
    if (arguments.wantsHelp()) { std::fputs(arguments.help().c_str(), stdout); return SDL_APP_SUCCESS; }
    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    auto *app = new Application(std::move(options));
    app->frames = arguments.frames;
    *appstate = app;
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialization failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  try {
    return static_cast<Application *>(appstate)->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  try {
    auto &app = *static_cast<Application *>(appstate);
    const auto result = app.engine.Iterate();
    return app.frames > 0 && --app.frames == 0 ? SDL_APP_SUCCESS : result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void SDL_AppQuit(void *appstate, SDL_AppResult) {
  delete static_cast<Application *>(appstate);
}
