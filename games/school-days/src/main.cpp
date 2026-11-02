#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "SchoolDaysAssets.hpp"
#include "SchoolDaysLauncher.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace {
Kotonoha::EngineOptions
WithAssets(Kotonoha::EngineOptions options,
           const std::shared_ptr<SchoolDays::Assets> &assets) {
  options.assets = assets;
  return options;
}

struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;
  Application(std::shared_ptr<SchoolDays::Assets> assets,
              const std::string &route, Kotonoha::EngineOptions options,
              const std::string &documents)
      : runtime(assets, route, documents),
        engine(runtime, WithAssets(std::move(options), assets)) {}
  ~Application() { runtime.shutdown(); }
};

struct App {
  std::unique_ptr<SchoolDays::Launcher> launcher;
  std::unique_ptr<Application> application;
  Kotonoha::EngineOptions options;
  std::string route, startNode, dump, documents;
  long frames = -1;
  void start(const std::string &directory) {
    auto assets = std::make_shared<SchoolDays::Assets>(
        directory, options.assetOverlayDirectory());
    assets->setOverlay(options.assetOverlayDirectory());
    application = std::make_unique<Application>(std::move(assets), route,
                                                options, documents);
    if (!startNode.empty()) {
      application->runtime.SetState(Kotonoha::GameState::Gameplay);
      application->engine.debugJumpToNode(startNode);
    }
    if (!dump.empty())
      application->engine.dumpKtrf(dump);
  }
};

} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  *appstate = nullptr;
  SDL_SetAppMetadata("School Days HQ", "0.1.0",
                     "me.hirameki.kotonoha.schooldays");
  try {
    auto app = std::make_unique<App>();
    std::string directory;
    app->options.persistSettings = true;

    Kotonoha::EngineOptions &options = app->options;

    Kotonoha::CommandLine arguments("SchoolDays", options);
    arguments.option("--school-days", "DIRECTORY",
                     "Retail Packs or extracted game directory", directory);
    arguments.alias("--school-days", "--assets");
    arguments.option("--ktrf", "FILE", "School Days routing graph", app->route);
    arguments.alias("--asset-overlay", "--scripts");
    arguments.option("--documents", "DIRECTORY",
                     "Save-game and GlobalFlag directory", app->documents);
    arguments.option("--node", "ID", "Start at an index or scene key",
                     app->startNode);
    arguments.option("--dump-ktrf", "FILE", "Export the active routing graph",
                     app->dump);
    arguments.parse(argc, argv);
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = arguments.frames;
    if (app->options.subtitleStylePath.empty())
      if (const auto styles = Kotonoha::FileResolver::findGameAsset(
              "school-days", "styles.ass"))
        app->options.subtitleStylePath = styles->string();
    if (app->options.enableMCP && app->options.settingsPath.empty())
      app->options.persistSettings = false;
    if (app->options.persistSettings && app->options.settingsPath.empty())
      app->options.settingsPath =
          Kotonoha::EngineSettings::defaultPath("school-days");
    const std::string settings =
        app->options.persistSettings ? app->options.settingsPath : "";

    app->route = Kotonoha::GameLauncher::findGamePath(
        "school-days",
        {app->route,
         Kotonoha::GameLauncher::rememberedPath(
             settings, SchoolDays::Launcher::RouteRememberFile),
         SchoolDays::Assets::findDefaultRoute().string()},
        SchoolDays::Launcher::validateRoute, {"school-days-hq.ktnroute"});
    const auto env = SDL_getenv("SCHOOL_DAYS_DIR");
    directory = Kotonoha::GameLauncher::findGamePath(
        "school-days",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settings, "dir.txt"),
         SchoolDays::Assets::defaultDirectory().string()},
        [](const std::string &path, std::string &info) {
          return SchoolDays::Assets::checkDirectory(path, info);
        });
    if (directory.empty() || app->route.empty())
      options.launcher = true;
    if (options.launcher) {
      app->launcher = std::make_unique<SchoolDays::Launcher>(
          settings, directory, app->route);
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *appstate = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SchoolDays initialization: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  auto *app = static_cast<App *>(appstate);
  if (!app)
    return SDL_APP_FAILURE;
  try {
    return app->launcher ? app->launcher->event(*event)
                         : app->application->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SchoolDays event: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  auto *app = static_cast<App *>(appstate);
  if (!app)
    return SDL_APP_FAILURE;
  try {
    if (app->launcher) {
      app->launcher->iterate();
      if (app->launcher->state() == SchoolDays::Launcher::State::Quit)
        return SDL_APP_SUCCESS;
      if (app->launcher->state() == SchoolDays::Launcher::State::Launch) {
        const auto directory = app->launcher->path();
        app->route = app->launcher->routePath();
        app->options.settings = app->launcher->settings();
        app->launcher.reset();
        app->start(directory);
      }
      return SDL_APP_CONTINUE;
    }
    const auto result = app->application->engine.Iterate();
    if (app->frames > 0 && --app->frames == 0)
      return SDL_APP_SUCCESS;
    return result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SchoolDays frame: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void SDL_AppQuit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}
