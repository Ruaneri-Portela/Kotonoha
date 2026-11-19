#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include "DdlcLauncher.hpp"
#include "DdlcRuntime.hpp"
#include <Ddlc/DdlcAssets.hpp>

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
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
  if (auto found = Kotonoha::FileResolver::findGameAsset(
          "katawa-shoujo", "game/script-a1-monday.rpyc"))
    return found->parent_path().string();
  if (auto found = Kotonoha::FileResolver::findGameAsset("katawa-shoujo",
                                                         "game/data.rpa"))
    return found->parent_path().string();
  return (Kotonoha::gameAssetDirectory("doki-doki-literature-club") / "game")
      .string();
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

    bool katawaRequested = SDL_getenv("KATAWA_DIR") != nullptr;
    for (int i = 1; i < argc; ++i)
      if (argv[i] && (std::string(argv[i]) == "--katawa" ||
                      std::string(argv[i]).compare(0, 8, "--katawa=") == 0))
        katawaRequested = true;
    if (katawaRequested)
      KOTONOHA_SET_APP_METADATA("Katawa Shoujo",
                                "me.hirameki.kotonoha.katawashoujo");
    else
      KOTONOHA_SET_APP_METADATA("Doki Doki Literature Club!",
                                "me.hirameki.kotonoha.dokidokiliteratureclub");

    Kotonoha::CommandLine arguments("DokiDoki", options);
    arguments.option("--ddlc", "DIRECTORY|ISO",
                     "DDLC directory or ISO containing scripts.rpa", directory);
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
    const auto env = SDL_getenv("DDLC_DIR");
    const auto katawa = SDL_getenv("KATAWA_DIR");
    std::string selectedDirectory =
        directory.empty() ? (katawa ? katawa : (env ? env : "")) : directory;
    std::string validationInfo;
    if (!selectedDirectory.empty() &&
        Ddlc::GameLauncher::validatePath(selectedDirectory, validationInfo)) {
      directory = selectedDirectory;
    } else {
      if (!selectedDirectory.empty())
        SDL_Log("Katawa source validation: %s", validationInfo.c_str());
      directory = Kotonoha::GameLauncher::findGamePath(
          "doki-doki-literature-club",
          {directory, katawa ? katawa : "", env ? env : "",
           Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
           defaultDirectory()},
          Ddlc::GameLauncher::validatePath, {"", "game"});
    }
    const std::filesystem::path root(directory);
    const std::filesystem::path game =
        std::filesystem::is_regular_file(root / "script-a1-monday.rpyc") ||
                std::filesystem::is_regular_file(root / "data.rpa")
            ? root
            : root / "game";
    if (std::filesystem::is_regular_file(game / "script-a1-monday.rpyc")) {
      KOTONOHA_SET_APP_METADATA("Katawa Shoujo",
                                "me.hirameki.kotonoha.katawashoujo");
    } else {
      KOTONOHA_SET_APP_METADATA("Doki Doki Literature Club!",
                                "me.hirameki.kotonoha.dokidokiliteratureclub");
    }
    // Honor the shared default and explicit --no-launcher. Automated
    // --frames and MCP runs bypass the launcher in CommandLine::parse().
    if (directory.empty() && !options.launcher)
      throw std::runtime_error(
          "No valid DDLC source; pass --ddlc DIRECTORY|ISO");

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

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
