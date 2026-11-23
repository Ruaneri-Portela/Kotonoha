#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>
#include <SummerRadish/SummerAssets.hpp>
#include <SummerRuntime.hpp>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
namespace {
Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Kotonoha::Providers::SummerAssets> assets) {
  options.assets = std::move(assets);
  return options;
}
struct Game {
  SummerRadish::SummerRuntime runtime;
  Kotonoha::Engine engine;
  Game(std::shared_ptr<Kotonoha::Providers::SummerAssets> assets,
       Kotonoha::EngineOptions options)
      : runtime(assets, "Miss Each Other", "me.hirameki.kotonoha.meo"),
        engine(runtime, withAssets(std::move(options), assets)) {}
  ~Game() { runtime.shutdown(); }
};
struct App {
  Kotonoha::EngineOptions options;
  long frames = -1;
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  std::unique_ptr<Game> game;
  void start(const std::string &directory) {
    auto assets = std::make_shared<Kotonoha::Providers::SummerAssets>(
        std::filesystem::path(directory), options.assetOverlayDirectory());
    assets->setOverlay(options.assetOverlayDirectory());
    game = std::make_unique<Game>(assets, options);
  }
};
bool validate(const std::string &path, std::string &info) {
  try {
    auto probe = std::make_shared<Kotonoha::Providers::SummerAssets>(path);
    if (!probe->contains("SCRIPT/A-00.XML")) {
      info = "Miss Each Other opening script not found";
      return false;
    }
    info = "Miss Each Other data found";
    return true;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}
} // namespace
struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **state, int argc, char **argv);
  static SDL_AppResult event(void *state, SDL_Event *event);
  static SDL_AppResult iterate(void *state);
  static void quit(void *state, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **state, int argc, char **argv) {
  *state = nullptr;
  KOTONOHA_SET_APP_METADATA("Miss Each Other", "me.hirameki.kotonoha.meo");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("MEO", options);
    cli.option("--meo", "DIRECTORY|ISO", "Miss Each Other data directory",
               directory);
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
    const auto env = SDL_getenv("MEO_DIR");
    directory = Kotonoha::GameLauncher::findGamePath(
        "meo",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         Kotonoha::gameAssetDirectory("meo").string()},
        validate);
    // An explicit game source must not silently suppress the launcher.
    // --frames/--mcp and --no-launcher are handled by CommandLine.
    if (directory.empty() && !options.launcher)
      throw std::runtime_error(
          "No game directory specified; use --meo DIRECTORY");

    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "Miss Each Other", settingsPath, directory, validate,
          options.settings);
      app->launcher->setAudioChannels({"BGM", "Voice", "Se"});
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *state = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Miss Each Other: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult KotonohaPortCallbacks::event(void *state, SDL_Event *event) {
  try {
    auto &app = *static_cast<App *>(state);
    return app.launcher ? app.launcher->event(*event)
                        : app.game->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game event: %s", error.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult KotonohaPortCallbacks::iterate(void *state) {
  try {
    auto &app = *static_cast<App *>(state);
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
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game frame: %s", error.what());
    return SDL_APP_FAILURE;
  }
}
void KotonohaPortCallbacks::quit(void *state, SDL_AppResult) {
  delete static_cast<App *>(state);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
