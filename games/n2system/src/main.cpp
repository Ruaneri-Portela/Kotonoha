// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include <N2System/N2Runtime.hpp>

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<N2System::N2ArchiveAssets> assets) {
  options.assets = std::move(assets);
  return options;
}

bool validate(const std::string &path, std::string &info) {
  try {
    if (std::filesystem::exists(path + "/00.utf") ||
        std::filesystem::exists(path + "/01.utf") ||
        std::filesystem::exists(path + "/scenario.nss") ||
        std::filesystem::exists(path + "/n2system.nss") ||
        std::filesystem::exists(path + "/start.nss")) {
      info = "Valid N2System / ONScripter game directory found";
      return true;
    }
    auto source = Kotonoha::Filesystem::Filesystem::mount(path);
    if (source->exists("00.utf") || source->exists("01.utf") ||
        source->exists("scenario.nss") || source->exists("n2system.nss")) {
      info = "N2System / ONScripter script files found in virtual source";
      return true;
    }
    info = "Select a directory containing 00.utf/01.utf or .nss script files";
    return false;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
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
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  std::unique_ptr<Application> game;
  Kotonoha::EngineOptions options;
  long frames = -1;

  void start(const std::string &directory) {
    auto assets = std::make_shared<N2System::N2ArchiveAssets>();
    if (!directory.empty() && std::filesystem::is_directory(directory)) {
      // Mount directory assets if present
      for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && (entry.path().extension() == ".npa" || entry.path().extension() == ".afs")) {
          std::ifstream ifs(entry.path(), std::ios::binary);
          if (ifs) {
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)),
                                       std::istreambuf_iterator<char>());
            assets->mount(std::move(bytes));
          }
        }
      }
    }
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

SDL_AppResult KotonohaPortCallbacks::init(void **appstate, int argc,
                                          char **argv) {
  *appstate = nullptr;
  KOTONOHA_SET_APP_METADATA("N2System Game", "me.hirameki.kotonoha.n2system");
  try {
    auto app = std::make_unique<App>();
    Kotonoha::EngineOptions &options = app->options;
    options.persistSettings = true;
    std::string directory = "/Volumes/Nero 256GB/KotonohaAssets/SayaNoUta_PT-BR";
    Kotonoha::CommandLine cli("N2System", options);
    cli.option("--n2system", "DIRECTORY|NPA", "N2System game data directory",
               directory);
    cli.parse(argc, argv);
    if (cli.wantsHelp()) {
      std::fputs(cli.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = cli.frames;

    if (options.persistSettings && options.settingsPath.empty())
      options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    const auto settingsPath = options.persistSettings ? options.settingsPath : std::string();

    directory = Kotonoha::GameLauncher::findGamePath(
        "n2system",
        {directory, "/Volumes/Nero 256GB/KotonohaAssets/SayaNoUta_PT-BR",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt")},
        validate, {""}, true);

    if (directory.empty() || options.launcher) {
      options.launcher = true;
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "N2System", settingsPath, directory, validate, options.settings, true);
      app->launcher->open();
    } else {
      app->start(directory);
    }

    *appstate = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "N2System init failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::event(void *appstate, SDL_Event *event) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app) {
      if (app->launcher)
        return app->launcher->event(*event);
      if (app->game)
        return app->game->engine.Event(event);
    }
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "N2System event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app) {
      if (app->launcher) {
        app->launcher->iterate();
        if (app->launcher->state() == Kotonoha::Launcher::State::Quit)
          return SDL_APP_SUCCESS;
        if (app->launcher->state() == Kotonoha::Launcher::State::Launch) {
          const auto directory = app->launcher->path();
          app->options.settings = app->launcher->settings();
          app->launcher.reset();
          app->start(directory);
        }
        return SDL_APP_CONTINUE;
      }

      if (app->game) {
        Application &game = *app->game;
        const SDL_AppResult result = game.engine.Iterate();
        if (game.framesLeft > 0 && --game.framesLeft == 0)
          return SDL_APP_SUCCESS;
        return result;
      }
    }
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "N2System frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
