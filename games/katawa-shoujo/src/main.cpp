#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include <Ddlc/DdlcAssets.hpp>
#include <DdlcRuntime.hpp>

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
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
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
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
    game = std::make_unique<Application>(assets, options);
    game->framesLeft = frames;
  }
};

bool validateKatawa(const std::string &path, std::string &info) {
  const auto root = std::filesystem::path(path);
  const auto game =
      std::filesystem::is_regular_file(root / "script-a1-monday.rpyc")
          ? root
          : root / "game";
  if (!std::filesystem::is_regular_file(game / "script-a1-monday.rpyc")) {
    info = "Katawa Shoujo script-a1-monday.rpyc not found";
    return false;
  }
  info = "Katawa Shoujo data found";
  return true;
}

std::string defaultDirectory() {
  if (auto found = Kotonoha::FileResolver::findGameAsset(
          "katawa-shoujo", "game/script-a1-monday.rpyc"))
    return found->parent_path().string();
  if (auto found = Kotonoha::FileResolver::findGameAsset("katawa-shoujo",
                                                         "game/data.rpa"))
    return found->parent_path().string();
  return (Kotonoha::gameAssetDirectory("katawa-shoujo") / "game").string();
}

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
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;

    KOTONOHA_SET_APP_METADATA("Katawa Shoujo",
                              "me.hirameki.kotonoha.katawashoujo");

    Kotonoha::CommandLine arguments("KatawaShoujo", options);
    arguments.option("--katawa", "DIRECTORY|ISO",
                     "Katawa Shoujo directory or ISO containing game data",
                     directory);
    arguments.alias("--asset-overlay", "--overlay");
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
    const auto katawa = SDL_getenv("KATAWA_DIR");
    std::string selectedDirectory =
        directory.empty() ? (katawa ? katawa : "") : directory;
    std::string validationInfo;
    if (!selectedDirectory.empty() &&
        validateKatawa(selectedDirectory, validationInfo)) {
      directory = selectedDirectory;
    } else {
      if (!selectedDirectory.empty())
        SDL_Log("Katawa source validation: %s", validationInfo.c_str());
      directory = Kotonoha::GameLauncher::findGamePath(
          "katawa-shoujo",
          {directory, katawa ? katawa : "",
           Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
           defaultDirectory()},
          validateKatawa, {"", "game"});
    }
    // Honor the shared default and explicit --no-launcher. Automated
    // --frames and MCP runs bypass the launcher in CommandLine::parse().
    if (directory.empty() && !options.launcher)
      throw std::runtime_error(
          "No valid Katawa source; pass --katawa DIRECTORY|ISO");

    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "Katawa Shoujo", settingsPath, directory, validateKatawa,
          options.settings);
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

SDL_AppResult KotonohaPortCallbacks::event(void *appstate, SDL_Event *event) {
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

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app->launcher) {
      app->launcher->iterate();
      if (app->launcher->state() != Kotonoha::Launcher::State::Launch &&
          app->launcher->state() != Kotonoha::Launcher::State::Quit)
        return SDL_APP_CONTINUE;
      if (app->launcher->state() == Kotonoha::Launcher::State::Quit)
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

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
