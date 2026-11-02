#define SDL_MAIN_USE_CALLBACKS 1
#include "AirAssets.hpp"
#include "AirRuntime.hpp"
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <memory>
namespace {
Kotonoha::EngineOptions withAssets(Kotonoha::EngineOptions options,
                                   std::shared_ptr<Air::AirAssets> a) {
  options.assets = std::move(a);
  return options;
}
struct App {
  Air::AirRuntime game;
  Kotonoha::Engine engine;
  long frames;
  App(std::shared_ptr<Air::AirAssets> a, Kotonoha::EngineOptions options,
      long frames)
      : game(a), engine(game, withAssets(std::move(options), a)),
        frames(frames) {}
  ~App() { game.shutdown(); }
};
} // namespace
SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
  *state = nullptr;
  try {
    Kotonoha::EngineOptions options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("Air", options);
    cli.option("--air", "DIRECTORY", "AIR Standard Edition data directory",
               directory);
    cli.parse(argc, argv);
    if (cli.wantsHelp()) {
      std::fputs(cli.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    if (options.persistSettings && options.settingsPath.empty())
      options.settingsPath =
          Kotonoha::EngineSettings::defaultPath("me.hirameki.kotonoha.air");
    if (directory.empty()) {
      const char *env = SDL_getenv("AIR_DIR");
      if (env)
        directory = env;
      else {
        std::vector<std::filesystem::path> candidates = {
            "games/air/game-assets", std::filesystem::path(SDL_GetBasePath()) /
                                         "../Resources/game-assets"};
        if (const char *downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS))
          candidates.push_back(std::filesystem::path(downloads) /
                               "SekaiVN - AIR [BR - V2.0]");
        for (const auto &candidate : candidates)
          if (std::filesystem::exists(candidate / "Seen.txt")) {
            directory = candidate.string();
            break;
          }
        if (directory.empty())
          throw std::runtime_error(
              "Informe a pasta de AIR com --air DIRECTORY ou AIR_DIR.");
      }
    }
    auto assets = std::make_shared<Air::AirAssets>(
        directory, options.assetOverlayDirectory());
    *state = new App(assets, std::move(options), cli.frames);
    return SDL_APP_CONTINUE;
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AIR: %s", e.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult SDL_AppEvent(void *state, SDL_Event *event) {
  try {
    return static_cast<App *>(state)->engine.Event(event);
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AIR event: %s", e.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult SDL_AppIterate(void *state) {
  try {
    auto &app = *static_cast<App *>(state);
    auto result = app.engine.Iterate();
    return app.frames > 0 && --app.frames == 0 ? SDL_APP_SUCCESS : result;
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AIR frame: %s", e.what());
    return SDL_APP_FAILURE;
  }
}
void SDL_AppQuit(void *state, SDL_AppResult) {
  delete static_cast<App *>(state);
}
