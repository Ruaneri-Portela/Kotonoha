// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisRuntime.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Artemis::ArtemisAssets> assets) {
  options.assets = std::move(assets);
  return options;
}

bool validate(const std::string &path, std::string &info) {
  try {
    auto source = Kotonoha::Filesystem::Filesystem::mount(path);
    for (const char *marker :
         {"root.pfs", "root.pfs.000", "system.ini", "script/gamestart.ast"}) {
      if (Kotonoha::Filesystem::isGameSourceRegularFile(*source, marker)) {
        info = std::string("Artemis Engine source detected: ") + marker;
        return true;
      }
    }
    for (const auto &entry :
         Kotonoha::Filesystem::listGameSourceFiles(*source)) {
      if (entry.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      std::string lower = entry.path;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if ((lower.size() > 4 &&
           lower.compare(lower.size() - 4, 4, ".pfs") == 0) ||
          lower.find(".pfs.") != std::string::npos) {
        info = "Artemis Engine PFS archive found in virtual source";
        return true;
      }
    }
    info = "Select a directory containing Artemis Engine .pfs archives";
    return false;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}

struct App {
  Kotonoha::EngineOptions options;
  long frames = -1;
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  struct Game {
    Artemis::ArtemisRuntime runtime;
    Kotonoha::Engine engine;
    ~Game() { runtime.shutdown(); }
    Game(std::shared_ptr<Artemis::ArtemisAssets> a, Kotonoha::EngineOptions o)
        : runtime(a), engine(runtime, withAssets(std::move(o), std::move(a))) {}
  };
  std::unique_ptr<Game> game;

  void start(const std::string &directory) {
    auto assets = std::make_shared<Artemis::ArtemisAssets>(
        directory, options.assetOverlayDirectory());
    game = std::make_unique<Game>(std::move(assets), options);
  }
};

} // namespace

struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **state, int argc, char **argv);
  static SDL_AppResult event(void *state, SDL_Event *event);
  static SDL_AppResult iterate(void *state);
  static void quit(void *state, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **appstate, int argc,
                                          char **argv) {
  *appstate = nullptr;
  KOTONOHA_SET_APP_METADATA("Artemis Engine", "me.hirameki.kotonoha.artemis");
  try {
    auto app = std::make_unique<App>();
    Kotonoha::EngineOptions &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("ArtemisGame", options);
    cli.option("--artemis", "DIRECTORY|ISO",
               "Artemis Engine game data directory", directory);
    cli.parse(argc, argv);
    if (cli.wantsHelp()) {
      std::fputs(cli.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = cli.frames;

    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    if (options.persistSettings && options.settingsPath.empty())
      options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    const auto settingsPath =
        options.persistSettings ? options.settingsPath : std::string();
    const char *env = SDL_getenv("ARTEMIS_DIR");

    directory = Kotonoha::GameLauncher::findGamePath(
        "artemis",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         Kotonoha::gameAssetDirectory("artemis").string()},
        validate, {""}, true);

    if (directory.empty() && !options.launcher)
      throw std::runtime_error(
          "No valid Artemis source; pass --artemis DIRECTORY|ISO");
    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "Artemis Engine", settingsPath, directory, validate, options.settings,
          true);
      app->launcher->open();
    } else {
      app->start(directory);
    }

    *appstate = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Artemis init failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::event(void *appstate, SDL_Event *event) {
  try {
    auto &app = *static_cast<App *>(appstate);
    return app.launcher ? app.launcher->event(*event)
                        : app.game->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Artemis event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  try {
    auto &app = *static_cast<App *>(appstate);
    if (app.launcher) {
      app.launcher->iterate();
      if (app.launcher->state() == Kotonoha::Launcher::State::Quit)
        return SDL_APP_SUCCESS;
      if (app.launcher->state() != Kotonoha::Launcher::State::Launch)
        return SDL_APP_CONTINUE;
      const auto directory = app.launcher->path();
      app.options.settings = app.launcher->settings();
      app.launcher.reset();
      app.start(directory);
    }
    const auto result = app.game->engine.Iterate();
    return app.frames > 0 && --app.frames == 0 ? SDL_APP_SUCCESS : result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Artemis frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
