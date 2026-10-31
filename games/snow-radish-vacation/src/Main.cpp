#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "SnowAssets.hpp"
#include "SnowLauncher.hpp"
#include "SnowRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <cstdio>
#include <Kotonoha/core/FileSystem.hpp>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// The DAT directory is the engine AssetProvider; the runtime keeps the typed handle.
Kotonoha::EngineOptions WithAssets(Kotonoha::EngineOptions options, std::shared_ptr<Kotonoha::AssetProvider> assets) {
  options.assets = std::move(assets);
  return options;
}

struct Application {
  SnowRadish::SnowRuntime game;
  Kotonoha::Engine engine;
  long framesLeft = -1; // --frames: smoke runs quit by themselves

  Application(std::shared_ptr<SnowRadish::SnowAssets> assets, Kotonoha::EngineOptions options)
      : game(assets), engine(game, WithAssets(std::move(options), assets)) {}
  // The engine (renderer, mixer) goes before the runtime: free the session first.
  ~Application() { game.Shutdown(); }
};

// With --launcher the launcher picks the DAT folder and the engine settings first.
struct App {
  std::unique_ptr<SnowRadish::SnowLauncher> launcher;
  std::unique_ptr<Application> game;
  Kotonoha::EngineOptions options;
  long frames = -1;

  void Start(const std::string &directory) {
    auto assets = std::make_shared<SnowRadish::SnowAssets>(directory);
    game = std::make_unique<Application>(std::move(assets), options);
    game->framesLeft = frames;
  }
};

std::string DefaultDirectory() {
  const std::string game = "snow-radish-vacation";
  for (const char *script : {"SCRIPT.DAT", "script.dat"})
    if (auto found = Kotonoha::FileResolver::findGameAsset(game, script))
      return found->parent_path().string();
  return Kotonoha::gameAssetDirectory(game).string();
}


} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  *appstate = nullptr;
  SDL_SetAppMetadata("Snow Radish Vacation!!", "0.1.0", "me.hirameki.kotonoha.snowradishvacation");
  try {
    auto app = std::make_unique<App>();
    Kotonoha::EngineOptions &options = app->options;
    options.persistSettings = true;
    std::string directory;
    std::string dumpPath; // export every route and exit
    bool launcher = false; // opt-in
    Kotonoha::CommandLine arguments("SnowRadish", options);
    arguments.option("--snow", "DIRECTORY", "Snow Radish DAT directory", directory);
    arguments.option("--dump-translation", "FILE", "Export original texts by route and exit", dumpPath);
    arguments.flag("--launcher", "Open the DAT selection launcher", launcher);
    arguments.parse(argc, argv);
    if (arguments.wantsHelp()) { std::fputs(arguments.help().c_str(), stdout); return SDL_APP_SUCCESS; }
    app->frames = arguments.frames;
    if (options.enableMCP && options.settingsPath.empty()) options.persistSettings = false;
    if (options.settingsPath.empty() && options.persistSettings)
      options.settingsPath = Kotonoha::EngineSettings::defaultPath("snow-radish-vacation");
    const std::string settingsPath = options.persistSettings ? options.settingsPath : std::string();
    if (directory.empty())
      if (const char *value = std::getenv("SNOW_DIR"); value && *value) directory = value;
    if (directory.empty()) directory = DefaultDirectory();
    if (directory.empty()) directory = Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt");
    if (launcher) {
      app->launcher = std::make_unique<SnowRadish::SnowLauncher>(settingsPath, directory);
      app->launcher->open();
    } else {
      app->Start(directory);
    }
    *appstate = app.release();
    if (!dumpPath.empty()) {
      auto *started = static_cast<App *>(*appstate);
      if (!started->game) throw std::invalid_argument("--dump-translation cannot be combined with --launcher");
      started->game->engine.dumpTranslation(dumpPath);
      SDL_Log("Translation dictionary written to %s", dumpPath.c_str());
      return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialization failed: %s", error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app->launcher) return app->launcher->event(*event);
    return app->game->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s", error.what());
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
    if (game.framesLeft > 0 && --game.framesLeft == 0) return SDL_APP_SUCCESS;
    return result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s", error.what());
    return SDL_APP_FAILURE;
  }
}

void SDL_AppQuit(void *appstate, SDL_AppResult) { delete static_cast<App *>(appstate); }
