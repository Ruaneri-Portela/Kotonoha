// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include <N2System/N2Runtime.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

namespace {

Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<N2System::N2ArchiveAssets> assets) {
  options.assets = std::move(assets);
  return options;
}

struct Application {
  N2System::N2Runtime game;
  Kotonoha::Engine engine;
  long framesLeft = -1;

  Application(std::shared_ptr<N2System::N2ArchiveAssets> assets,
              Kotonoha::EngineOptions options)
      : game(assets), engine(game, withAssets(std::move(options), assets)) {}
};

struct App {
  std::unique_ptr<Application> game;
  Kotonoha::EngineOptions options;
  long frames = -1;

  void start(const std::string &directory) {
    (void)directory;
    auto assets = std::make_shared<N2System::N2ArchiveAssets>();
    game = std::make_unique<Application>(std::move(assets), options);
    game->framesLeft = frames;
  }
};

} // namespace

struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **state, int argc, char **argv);
  static SDL_AppResult event(void *state, SDL_Event *event);
  static SDL_AppResult iterate(void *state);
  static void quit(void *state, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **appstate, int argc, char **argv) {
  *appstate = nullptr;
  KOTONOHA_SET_APP_METADATA("N2System Game", "me.hirameki.kotonoha.n2system");
  try {
    auto app = std::make_unique<App>();
    Kotonoha::EngineOptions &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("N2System", options);
    cli.option("--n2system", "DIRECTORY|NPA", "N2System game data directory", directory);
    cli.parse(argc, argv);
    if (cli.wantsHelp()) {
      std::fputs(cli.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = cli.frames;
    app->start(directory);
    *appstate = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "N2System init failed: %s", error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::event(void *appstate, SDL_Event *event) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app && app->game)
      return app->game->engine.Event(event);
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "N2System event failed: %s", error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app && app->game) {
      Application &game = *app->game;
      const SDL_AppResult result = game.engine.Iterate();
      if (game.framesLeft > 0 && --game.framesLeft == 0)
        return SDL_APP_SUCCESS;
      return result;
    }
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "N2System frame failed: %s", error.what());
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
