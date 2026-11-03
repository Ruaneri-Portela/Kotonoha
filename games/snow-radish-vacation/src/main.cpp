#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "SnowAssets.hpp"
#include "SnowLauncher.hpp"
#include "SnowRuntime.hpp"

#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <cstdio>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// The DAT directory is the engine AssetProvider; the runtime keeps the typed
// handle.
Kotonoha::EngineOptions
WithAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Kotonoha::AssetProvider> assets) {
  options.assets = std::move(assets);
  return options;
}

struct Application {
  SnowRadish::SnowRuntime game;
  Kotonoha::Engine engine;
  long framesLeft = -1; // --frames: smoke runs quit by themselves

  Application(std::shared_ptr<SnowRadish::SnowAssets> assets,
              Kotonoha::EngineOptions options)
      : game(assets), engine(game, WithAssets(std::move(options), assets)) {}
  // The engine (renderer, mixer) goes before the runtime: free the session
  // first.
  ~Application() { game.Shutdown(); }
};

// With --launcher the launcher picks the DAT folder and the engine settings
// first.
struct App {
  std::unique_ptr<SnowRadish::SnowLauncher> launcher;
  std::unique_ptr<Application> game;
  Kotonoha::EngineOptions options;
  long frames = -1;

  void Start(const std::string &directory) {
    auto assets = std::make_shared<SnowRadish::SnowAssets>(directory);
    assets->setOverlay(options.assetOverlayDirectory());
    game = std::make_unique<Application>(std::move(assets), options);
    game->framesLeft = frames;
  }
};

std::string DefaultDirectory() {
  for (const char *script : {"SCRIPT.DAT", "script.dat"})
    if (auto found = Kotonoha::FileResolver::findGameAsset({}, script))
      return found->parent_path().string();
  return Kotonoha::gameAssetDirectory({}).string();
}

} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  *appstate = nullptr;
  SDL_SetAppMetadata("Snow Radish Vacation!!", "0.1.0",
                     "me.hirameki.kotonoha.snowradishvacation");
  try {
    auto app = std::make_unique<App>();
    Kotonoha::EngineOptions &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine arguments("SnowRadish", options);
    arguments.option("--snow", "DIRECTORY", "Snow Radish DAT directory",
                     directory);
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
    const auto env = SDL_getenv("SNOW_DIR");
    directory = Kotonoha::GameLauncher::findGamePath(
        "snow-radish-vacation",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         DefaultDirectory()},
        SnowRadish::SnowLauncher::validatePath, {""});
    if (directory.empty())
      options.launcher = true;

    if (options.launcher) {
      app->launcher =
          std::make_unique<SnowRadish::SnowLauncher>(settingsPath, directory);
      app->launcher->open();
    } else {
      app->Start(directory);
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
      switch (app->launcher->state()) {
      case SnowRadish::SnowLauncher::State::Running:
        return SDL_APP_CONTINUE;
      case SnowRadish::SnowLauncher::State::Quit:
        return SDL_APP_SUCCESS;
      case SnowRadish::SnowLauncher::State::Launch:
        break;
      }
      // The launcher window and ImGui context go before the engine starts.
      const std::string directory = app->launcher->path();
      app->options.settings = app->launcher->settings();
      app->launcher.reset();
      app->Start(directory);
      return SDL_APP_CONTINUE;
    }
    Application &game = *app->game;
    const SDL_AppResult result = game.engine.Iterate();
    if (game.framesLeft > 0 && --game.framesLeft == 0)
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
