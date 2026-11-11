#define SDL_MAIN_USE_CALLBACKS 1
#include <RealLive/RealLiveAssets.hpp>
#include "RealLiveRuntime.hpp"
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
namespace {
Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Kotonoha::Providers::RealLiveAssets> assets) {
  options.assets = std::move(assets);
  return options;
}
struct Game {
  RealLive::RealLiveRuntime runtime;
  Kotonoha::Engine engine;
  Game(std::shared_ptr<Kotonoha::Providers::RealLiveAssets> assets,
       Kotonoha::EngineOptions options)
      : runtime(assets),
        engine(runtime, withAssets(std::move(options), assets)) {}
  ~Game() { runtime.shutdown(); }
};
struct App {
  Kotonoha::EngineOptions options;
  long frames = -1;
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  std::unique_ptr<Game> game;
  void start(const std::string &directory) {
    auto assets = std::make_shared<Kotonoha::Providers::RealLiveAssets>(
        directory, options.assetOverlayDirectory());
    game = std::make_unique<Game>(assets, options);
  }
};
bool validate(const std::string &path, std::string &info) {
  try {
    if (Kotonoha::isISOImage(path)) {
      Kotonoha::DiscImage disc(path);
      if (!disc.find("Seen.txt") || !disc.find("Gameexe.ini"))
        throw std::runtime_error(
            "REALLIVE ISO is missing Seen.txt or Gameexe.ini");
      info = "Game data found (direct ISO access)";
      return true;
    }
    std::set<std::string> files;
    for (const auto &entry : std::filesystem::directory_iterator(path))
      if (entry.is_regular_file())
        files.insert(RealLive::canonical(entry.path().filename().string()));
    if (!files.count("SEEN.TXT") || !files.count("GAMEEXE.INI")) {
      info = "Seen.txt or Gameexe.ini is missing";
      return false;
    }
    info = "Game data found";
    return true;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}
} // namespace
SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
  *state = nullptr;
  SDL_SetAppMetadata("REALLIVE", "0.1.0", "me.hirameki.kotonoha.reallive");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("RealLive", options);
    cli.option("--reallive", "DIRECTORY|ISO",
               "Game data directory or ISO image", directory);
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
    const auto env = SDL_getenv("REALLIVE_DIR");
    const auto downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
    directory = Kotonoha::GameLauncher::findGamePath(
        "reallive",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         Kotonoha::gameAssetDirectory("reallive").string(),
         downloads ? (std::filesystem::path(downloads) /
                      "SekaiVN - REALLIVE [BR - V2.0]")
                         .string()
                   : ""},
        validate, {""}, true);
    if (directory.empty())
      options.launcher = true;
    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "REALLIVE", settingsPath, directory, validate, options.settings,
          true);
      app->launcher->setAudioChannels({"BGM", "Voice", "PCM", "Se"});
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *state = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "REALLIVE: %s", error.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult SDL_AppEvent(void *state, SDL_Event *event) {
  try {
    auto &app = *static_cast<App *>(state);
    return app.launcher ? app.launcher->event(*event)
                        : app.game->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game event: %s", error.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult SDL_AppIterate(void *state) {
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
void SDL_AppQuit(void *state, SDL_AppResult) {
  delete static_cast<App *>(state);
}
