#define SDL_MAIN_USE_CALLBACKS 1
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets) {
  options.assets = std::move(assets);
  return options;
}

struct App {
  Kotonoha::EngineOptions options;
  long frames = -1;
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  struct Game {
    LucaSystem::LucaSystemRuntime runtime;
    Kotonoha::Engine engine;
    Game(std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> a,
         Kotonoha::EngineOptions o)
        : runtime(std::move(a)), engine(runtime, std::move(o)) {}
  };
  std::unique_ptr<Game> game;

  void start(const std::string &directory) {
    auto assets = std::shared_ptr<Kotonoha::Providers::LucaSystemAssets>(
        new Kotonoha::Providers::LucaSystemAssets(
            directory, options.assetOverlayDirectory()));
    game = std::make_unique<Game>(std::move(assets), std::move(options));
  }
};

bool validate(const std::string &path, std::string &info) {
  try {
    if (Kotonoha::isISOImage(path)) {
      Kotonoha::DiscImage disc(path);
      info = "Game data found (direct ISO access)";
      return true;
    }
    auto dataPath = std::filesystem::path(path);
    if (!std::filesystem::is_regular_file(dataPath / "SCRIPT.PAK") &&
        std::filesystem::is_regular_file(dataPath / "files" / "SCRIPT.PAK"))
      dataPath /= "files";
    std::set<std::string> corePaks;
    for (const auto &entry : std::filesystem::directory_iterator(dataPath)) {
      if (!entry.is_regular_file())
        continue;
      auto name = entry.path().filename().string();
      if (name == "PARAM.PAK" || name == "SCRIPT.PAK" || name == "SYSSE.PAK")
        corePaks.insert(name);
    }
    if (corePaks.size() != 3) {
      info = "No LucaSystem archive (PARAM.PAK/SCRIPT.PAK/SYSSE.PAK) found";
      return false;
    }
    info = "LucaSystem game data found";
    return true;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}

} // namespace

SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
  *state = nullptr;
  SDL_SetAppMetadata("LucaSystem", "0.1.0", "me.hirameki.kotonoha.luca_system");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("LucaSystem", options);
    cli.option("--luca-system", "DIRECTORY|ISO", "LucaSystem game data",
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
    const auto env = SDL_getenv("LUCA_SYSTEM_DIR");
    const auto downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
    directory = Kotonoha::GameLauncher::findGamePath(
        "luca_system",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         Kotonoha::gameAssetDirectory("luca_system").string(),
         downloads ? (std::filesystem::path(downloads) / "LucaSystem").string()
                   : ""},
        validate, {""}, true);
    if (directory.empty())
      options.launcher = true;
    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "LucaSystem", settingsPath, directory, validate, options.settings,
          true);
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *state = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: %s", error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *state, SDL_Event *event) {
  try {
    auto &app = *static_cast<App *>(state);
    if (app.launcher)
      return app.launcher->event(*event);
    return app.game->engine.Event(event);
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
