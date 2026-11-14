#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "DdlcLauncher.hpp"
#include "DdlcRuntime.hpp"
#include <Ddlc/DdlcAssets.hpp>

#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <cstdio>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           const std::shared_ptr<Kotonoha::Providers::DdlcAssets> &assets) {
  options.assets = assets;
  return options;
}

struct Application {
  Ddlc::GameRuntime game;
  Kotonoha::Engine engine;
  long framesLeft = -1;

  Application(const std::shared_ptr<Kotonoha::Providers::DdlcAssets> &assets,
              Kotonoha::EngineOptions options)
      : game(assets), engine(game, withAssets(std::move(options), assets)) {}
  ~Application() { game.shutdown(); }
};

struct App {
  std::unique_ptr<Ddlc::GameLauncher> launcher;
  std::unique_ptr<Application> game;
  Kotonoha::EngineOptions options;
  long frames = -1;

  void start(const std::string &directory) {
    std::shared_ptr<Kotonoha::Providers::DdlcAssets> assets;
    if (std::filesystem::is_regular_file(directory)) {
      auto mounted = Kotonoha::Filesystem::Filesystem::mount(directory);
      if (!options.assetOverlayDirectory().empty())
        mounted->addOverlay(options.assetOverlayDirectory());
      assets = std::make_shared<Kotonoha::Providers::DdlcAssets>(
          mounted, options.assetOverlayDirectory());
    } else {
      assets = std::make_shared<Kotonoha::Providers::DdlcAssets>(
          std::filesystem::path(directory), options.assetOverlayDirectory());
    }
    assets->setOverlay(options.assetOverlayDirectory());
    game = std::make_unique<Application>(assets, options);
    game->framesLeft = frames;
  }
};

// Game data: <root>/game-assets/game (bundles, OBB, working or executable
// directory, documents), then <root>/game and the source checkout layouts.
std::string defaultDirectory() {
  if (auto found = Kotonoha::FileResolver::findGameAsset(
          "doki-doki-literature-club", "game/scripts.rpa"))
    return found->parent_path().string();
  return (Kotonoha::gameAssetDirectory("doki-doki-literature-club") / "game")
      .string();
}

} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  *appstate = nullptr;
  SDL_SetAppMetadata("Doki Doki Literature Club!", "0.1.0",
                     "me.hirameki.kotonoha.dokidokiliteratureclub");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;

    Kotonoha::CommandLine arguments("DokiDoki", options);
    arguments.option("--ddlc", "DIRECTORY|ISO",
                     "DDLC directory or ISO containing scripts.rpa", directory);
    arguments.parse(argc, argv);
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = arguments.frames;
    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    if (options.settingsPath.empty() && options.persistSettings)
      options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    const std::string settingsPath =
        options.persistSettings ? options.settingsPath : std::string();
    const auto env = SDL_getenv("DDLC_DIR");
    directory = Kotonoha::GameLauncher::findGamePath(
        "doki-doki-literature-club",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         defaultDirectory()},
        Ddlc::GameLauncher::validatePath, {"", "game"});
    if (directory.empty())
      options.launcher = true;

    if (options.launcher) {
      app->launcher =
          std::make_unique<Ddlc::GameLauncher>(settingsPath, directory);
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *appstate = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialization failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app->launcher)
      return app->launcher->event(*event);
    return app->game->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app->launcher) {
      app->launcher->iterate();
      if (app->launcher->state() == Ddlc::GameLauncher::State::Running)
        return SDL_APP_CONTINUE;
      if (app->launcher->state() == Ddlc::GameLauncher::State::Quit)
        return SDL_APP_SUCCESS;
      const std::string directory = app->launcher->path();
      app->options.settings = app->launcher->settings();
      app->launcher.reset();
      app->start(directory);
      return SDL_APP_CONTINUE;
    }
    const SDL_AppResult result = app->game->engine.Iterate();
    if (app->game->framesLeft > 0 && --app->game->framesLeft == 0)
      return SDL_APP_SUCCESS;
    return result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void SDL_AppQuit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}
