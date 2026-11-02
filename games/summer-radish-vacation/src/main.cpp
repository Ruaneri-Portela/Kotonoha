#define SDL_MAIN_USE_CALLBACKS 1
#include "SummerAssets.hpp"
#include "SummerRuntime.hpp"
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <memory>
namespace {
Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<SummerRadish::SummerAssets> a) {
  options.assets = std::move(a);
  return options;
}
struct App {
  SummerRadish::SummerRuntime game;
  Kotonoha::Engine engine;
  long frames;
  App(std::shared_ptr<SummerRadish::SummerAssets> a,
      Kotonoha::EngineOptions options, long frames)
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
    std::string directory, dump;
    Kotonoha::CommandLine cli("SummerRadish", options);
    cli.option("--summer", "DIRECTORY",
               "Summer Radish disc root, DISC1 or PACK directory", directory);
    cli.option("--dump-translation", "FILE", "Export original texts and exit",
               dump);
    cli.parse(argc, argv);
    if (cli.wantsHelp()) {
      std::fputs(cli.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    if (directory.empty()) {
      const char *env = SDL_getenv("SUMMER_DIR");
      directory = env ? env : "games/summer-radish-vacation/game-assets";
    }
    auto assets = std::make_shared<SummerRadish::SummerAssets>(directory);
    if (!dump.empty()) {
      SummerRadish::SummerRuntime game(assets);
      Kotonoha::Translation translation;
      if (!options.translationPath.empty())
        translation.load(options.translationPath);
      translation.dump(dump, game.translationSources());
      return SDL_APP_SUCCESS;
    }
    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    *state = new App(assets, std::move(options), cli.frames);
    return SDL_APP_CONTINUE;
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Summer Radish: %s", e.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult SDL_AppEvent(void *state, SDL_Event *event) {
  try {
    return static_cast<App *>(state)->engine.Event(event);
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Summer event: %s", e.what());
    return SDL_APP_FAILURE;
  }
}
SDL_AppResult SDL_AppIterate(void *state) {
  try {
    auto &app = *static_cast<App *>(state);
    auto result = app.engine.Iterate();
    return app.frames > 0 && --app.frames == 0 ? SDL_APP_SUCCESS : result;
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Summer frame: %s", e.what());
    return SDL_APP_FAILURE;
  }
}
void SDL_AppQuit(void *state, SDL_AppResult) {
  delete static_cast<App *>(state);
}
