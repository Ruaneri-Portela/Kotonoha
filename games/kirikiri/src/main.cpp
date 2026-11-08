#define SDL_MAIN_USE_CALLBACKS 1
#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/KirikiriRuntime.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace {

Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Kirikiri::KirikiriAssets> assets) {
  options.assets = std::move(assets);
  return options;
}

struct App {
  Kotonoha::EngineOptions options;
  long frames = -1;
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  struct Game {
    Kirikiri::KirikiriRuntime runtime;
    Kotonoha::Engine engine;
    Game(std::shared_ptr<Kirikiri::KirikiriAssets> a, Kotonoha::EngineOptions o)
        : runtime(std::move(a)), engine(runtime, std::move(o)) {}
  };
  std::unique_ptr<Game> game;

  void start(const std::string &directory) {
    auto assets =
        std::shared_ptr<Kirikiri::KirikiriAssets>(new Kirikiri::KirikiriAssets(
            directory, options.assetOverlayDirectory()));
    // Auto-mount any XP3 archives found at the game root.
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
      if (entry.is_regular_file()) {
        auto ext = entry.path().extension().string();
        for (auto &c : ext)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".xp3")
          assets->mountXp3(entry.path());
      }
    }
    game = std::make_unique<Game>(std::move(assets), std::move(options));
  }
};

bool validate(const std::string &path, std::string &info) {
  try {
    if (Kotonoha::isISOImage(path)) {
      Kotonoha::DiscImage disc(path);
      // Accept any Kirikiri data: look for scenario scripts or XP3.
      info = "Game data found (direct ISO access)";
      return true;
    }
    bool hasScenario = false;
    bool hasXp3 = false;
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      if (!entry.is_regular_file())
        continue;
      auto name = entry.path().filename().string();
      auto ext = entry.path().extension().string();
      for (auto &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (ext == ".ks")
        hasScenario = true;
      if (ext == ".xp3")
        hasXp3 = true;
    }
    if (!hasScenario && !hasXp3) {
      info = "No Kirikiri scenario (.ks) or XP3 archive found";
      return false;
    }
    info = "Kirikiri game data found";
    return true;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}

} // namespace

SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
  *state = nullptr;
  SDL_SetAppMetadata("Kirikiri", "0.1.0", "me.hirameki.kotonoha.kirikiri");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("Kirikiri", options);
    cli.option("--kirikiri", "DIRECTORY|ISO", "Kirikiri2/KirikiriZ game data",
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
    const auto env = SDL_getenv("KIRIKIRI_DIR");
    const auto downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
    directory = Kotonoha::GameLauncher::findGamePath(
        "kirikiri",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         Kotonoha::gameAssetDirectory("kirikiri").string(),
         downloads ? (std::filesystem::path(downloads) / "Kirikiri").string()
                   : ""},
        validate, {""}, true);
    if (directory.empty())
      options.launcher = true;
    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "Kirikiri", settingsPath, directory, validate, options.settings,
          true);
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *state = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Kirikiri: %s", error.what());
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
