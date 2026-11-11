#define SDL_MAIN_USE_CALLBACKS 1
#include "AmagamiLauncher.hpp"
#include "AmagamiRuntime.hpp"
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
struct Application {
  Amagami::AmagamiRuntime game;
  Kotonoha::Engine engine;
  long frames = -1;
  Application(std::shared_ptr<Kotonoha::Providers::AmagamiAssets> assets,
              Kotonoha::EngineOptions options, std::string start)
      : game(assets, std::move(start)),
        engine(game, withAssets(std::move(options), assets)) {}
  static Kotonoha::EngineOptions
  withAssets(Kotonoha::EngineOptions options,
             std::shared_ptr<Kotonoha::Providers::AmagamiAssets> assets) {
    options.assets = std::move(assets);
    return options;
  }
  ~Application() { game.shutdown(); }
};

struct App {
  std::unique_ptr<Amagami::AmagamiLauncher> launcher;
  std::unique_ptr<Application> game;
  Kotonoha::EngineOptions options;
  std::string start;
  long frames = -1;
  void launch(const std::string &disc) {
    auto source = std::make_shared<Amagami::Source>(disc);
    auto assets = std::make_shared<Kotonoha::Providers::AmagamiAssets>(source);
    assets->setOverlay(options.assetOverlayDirectory());
    game = std::make_unique<Application>(assets, options, start);
    game->frames = frames;
  }
};

std::string DefaultDisc() {
  for (const char *disc : {"Amagami.bin", "Amagami.iso", "Amagami (Japan).iso"})
    if (auto found = Kotonoha::FileResolver::findGameAsset("amagami", disc))
      return found->string();
  return (Kotonoha::gameAssetDirectory("amagami") / "Amagami.bin").string();
}

} // namespace

SDL_AppResult SDL_AppInit(void **state, int argc, char **argv) {
  *state = nullptr;
  SDL_SetAppMetadata("Amagami", "0.1.0", "me.hirameki.kotonoha.amagami");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string disc;
    Kotonoha::CommandLine args("Amagami", options);
    args.option("--amagami", "DISC.iso|bin", "Amagami Japan PS2 disc", disc);
    args.option("--start", "SCENARIO", "Start a scenario (default: title)",
                app->start);
    args.parse(argc, argv);
    if (args.wantsHelp()) {
      std::fputs(args.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = args.frames;
    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    if (options.settingsPath.empty() && options.persistSettings)
      options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    const auto settings =
        options.persistSettings ? options.settingsPath : std::string();
    const char *env = SDL_getenv("AMAGAMI_DISC");
    disc = Kotonoha::GameLauncher::findGamePath(
        "amagami",
        {disc, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settings, "disc.txt"),
         DefaultDisc()},
        Amagami::AmagamiLauncher::validatePath, {"Amagami.bin"});
    if (disc.empty()) {
      if (args.frames > 0 || options.enableMCP)
        throw std::runtime_error("No Amagami disc; pass --amagami DISC.bin");
      options.launcher = true;
    }
    if (options.launcher) {
      app->launcher =
          std::make_unique<Amagami::AmagamiLauncher>(settings, disc);
      app->launcher->open();
    } else
      app->launch(disc);
    *state = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Amagami initialization: %s",
                 e.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *state, SDL_Event *event) {
  try {
    auto &app = *static_cast<App *>(state);
    return app.launcher ? app.launcher->event(*event)
                        : app.game->engine.Event(event);
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Amagami event: %s", e.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppIterate(void *state) {
  try {
    auto &app = *static_cast<App *>(state);
    if (app.launcher) {
      app.launcher->iterate();
      auto status = app.launcher->state();
      if (status == Kotonoha::Launcher::State::Quit)
        return SDL_APP_SUCCESS;
      if (status == Kotonoha::Launcher::State::Launch) {
        auto path = app.launcher->path();
        app.options.settings = app.launcher->settings();
        app.launcher.reset();
        app.launch(path);
      }
      return SDL_APP_CONTINUE;
    }
    auto result = app.game->engine.Iterate();
    return app.game->frames > 0 && --app.game->frames == 0 ? SDL_APP_SUCCESS
                                                           : result;
  } catch (const std::exception &e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Amagami frame: %s", e.what());
    return SDL_APP_FAILURE;
  }
}

void SDL_AppQuit(void *state, SDL_AppResult) {
  delete static_cast<App *>(state);
}
