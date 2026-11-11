#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "SchoolDaysLauncher.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <SchoolDays/SchoolDaysAssets.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
Kotonoha::EngineOptions WithAssets(
    Kotonoha::EngineOptions options,
    const std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> &assets) {
  options.assets = assets;
  return options;
}

struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;
  Application(std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> assets,
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
  std::string scripts, route, startNode, dump, documents;
  long frames = -1;
  void start(const std::string &directory) {
    auto assets = std::make_shared<Kotonoha::Providers::SchoolDaysAssets>(
        Kotonoha::gameDataDirectory(directory),
        scripts.empty() ? options.assetOverlayDirectory() : scripts);
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
    // The retail profile starts the game directly. CommandLine already owns
    // --launcher/--no-launcher and writes this EngineOptions field.
    app->options.launcher = false;
#if defined(_WIN32) && !defined(__ANDROID__)
    // The School Days commercial-test profile exposed Anime4K/ArtCNN to the
    // player.  In v3 the MPV-compatible upscaler is hosted by SDL's OpenGL
    // renderer, so prefer that renderer for a fresh/default Windows launch.
    // Command-line renderer selection is parsed below and can still override
    // this profile when explicitly requested.
    app->options.rendererOverride = "opengl";
    app->options.settings.mode = Kotonoha::WindowMode::Borderless;
    app->options.settings.vsync = true;
    app->options.settings.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
    app->options.settings.upscaling.preset = Kotonoha::UpscalePreset::ArtCNN;
    app->options.settings.upscaling.factor = 1.5f;
#elif defined(__ANDROID__) || defined(ANDROID)
    // Match the commercial mobile-low profile: keep the physical Surface at
    // native resolution, render the Novel layer at 75%, then apply the light
    // ArtCNN preset. Frame-rate selection itself is device/mode dependent and
    // is exposed by the School Days Android Display panel.
    app->options.rendererOverride = "opengles2";
    app->options.settings.mode = Kotonoha::WindowMode::Borderless;
    app->options.settings.vsync = true;
    app->options.settings.renderScale = 0.75f;
    app->options.settings.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
    app->options.settings.upscaling.preset = Kotonoha::UpscalePreset::ArtCNN;
    app->options.settings.upscaling.factor = 2.0f;
#endif
    Kotonoha::CommandLine arguments("SchoolDays", app->options);
    arguments.option("--school-days", "DIRECTORY|ISO",
                     "Retail Packs, extracted game directory or ISO",
                     directory);
    arguments.alias("--school-days", "--assets");
    arguments.option("--ktrf", "FILE", "School Days routing graph", app->route);
    arguments.option("--scripts", "DIRECTORY",
                     "Translated ORS script directory", app->scripts);
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
    if (app->options.persistSettings && app->options.settingsPath.empty()) {
      app->options.settingsPath =
          Kotonoha::EngineSettings::defaultPath("school-days");
    }
    const std::string settings =
        app->options.persistSettings ? app->options.settingsPath : "";
    // Route precedence: an explicit --ktrf wins, then the user's remembered
    // selection, and only then the bundled original-profile route.
    const auto rememberedValidRoute = [](const std::string &settingsPath) {
      if (settingsPath.empty())
        return std::string{};
      const auto path = Kotonoha::GameLauncher::rememberedPath(
          settingsPath, SchoolDays::Launcher::RouteRememberFile);
      std::string info;
      return !path.empty() && SchoolDays::Launcher::validateRoute(path, info)
                 ? path
                 : std::string{};
    };
    if (app->route.empty())
      app->route = rememberedValidRoute(settings);
    if (app->route.empty())
      app->route =
          Kotonoha::Providers::SchoolDaysAssets::defaultRoute().string();

    if (directory.empty())
      if (const char *env = std::getenv("SCHOOL_DAYS_DIR"))
        directory = env;
    const auto rememberedValidDirectory = [](const std::string &settingsPath) {
      if (settingsPath.empty())
        return std::string{};
      const auto path =
          Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt");
      std::string info;
      return !path.empty() &&
                     Kotonoha::Providers::SchoolDaysAssets::checkDirectory(path,
                                                                           info)
                 ? path
                 : std::string{};
    };
    if (directory.empty())
      directory = rememberedValidDirectory(settings);
    if (directory.empty())
      directory =
          Kotonoha::Providers::SchoolDaysAssets::defaultDirectory().string();
    if (app->options.launcher) {
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
#if defined(_WIN32) && !defined(__ANDROID__)
    // A double-clicked Windows build has no console to show the startup error.
    (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                   "School Days HQ initialization failed",
                                   error.what(), nullptr);
#endif
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  auto *app = static_cast<App *>(appstate);
  if (!app)
    return SDL_APP_FAILURE;
  try {
    if (!app->launcher && app->application &&
        (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
         event->type == SDL_EVENT_QUIT) &&
        app->application->runtime.handleNativeQuitRequest())
      return SDL_APP_CONTINUE;
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
    static bool firstFrameLogged = false;
    const auto result = app->application->engine.Iterate();
    if (!firstFrameLogged) {
      firstFrameLogged = true;
    }

    if (app->frames > 0 && --app->frames == 0)
      return SDL_APP_SUCCESS;
    return result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SchoolDays frame: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  delete static_cast<App *>(appstate);
}
