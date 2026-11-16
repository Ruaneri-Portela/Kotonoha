#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include "DemoGame.hpp"
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
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

// The port owns game-specific initialization and transitions. The shared
// SDLCallbackApp.hpp defines the four SDL callback entrypoints below.
struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **state, int argc, char **argv);
  static SDL_AppResult event(void *state, SDL_Event *event);
  static SDL_AppResult iterate(void *state);
  static void quit(void *state, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **appstate, int argc,
                                          char **argv) {
  *appstate = nullptr;
  KOTONOHA_SET_APP_METADATA("Kotonoha", "org.kotonoha.example");
  try {
    Kotonoha::migrateLegacyDocuments("me.hirameki.kotonoha");
    Kotonoha::EngineOptions options;
    options.persistSettings = true;
    Kotonoha::CommandLine arguments("Kotonoha", options);
    arguments.parse(argc, argv);
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
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

SDL_AppResult KotonohaPortCallbacks::event(void *appstate, SDL_Event *event) {
  try {
    return static_cast<Application *>(appstate)->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
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

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<Application *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
