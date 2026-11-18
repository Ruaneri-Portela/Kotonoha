// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include <KotonohaLauncher/UnifiedLauncher.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <cstdio>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
struct Application {
  std::string argv0;
  Kotonoha::EngineOptions options;
  std::string libraryPath;
  std::string customSaveRoot;
  bool splitSavesByGame = true;
  std::unique_ptr<KotonohaLauncher::UnifiedLauncher> launcher;
  std::unique_ptr<Kotonoha::Runtime> runtime;
  std::shared_ptr<Kotonoha::AssetProvider> assets;
  std::unique_ptr<Kotonoha::Engine> engine;
  long frames = -1;

  bool startInProcess(const KotonohaLauncher::DiscoveredGameEntry &entry) {
    KotonohaLauncher::applyPerGameSaveIsolation(
        entry, options, splitSavesByGame, customSaveRoot);
    auto mounted = KotonohaLauncher::EngineLibraryRegistry::instance()
                       .instantiate(entry, options);
    if (!mounted.runtime)
      return false;
    runtime = std::move(mounted.runtime);
    assets = std::move(mounted.assets);
    if (assets)
      options.assets = assets;
    engine = std::make_unique<Kotonoha::Engine>(*runtime, options);
    return true;
  }
};
} // namespace

struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **state, int argc, char **argv);
  static SDL_AppResult event(void *state, SDL_Event *event);
  static SDL_AppResult iterate(void *state);
  static void quit(void *state, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **appstate, int argc,
                                          char **argv) {
  *appstate = nullptr;
  KOTONOHA_SET_APP_METADATA("KotonohaLauncher",
                            "me.hirameki.kotonoha.launcher");
  try {
    auto app = std::make_unique<Application>();
    if (argc > 0 && argv && argv[0])
      app->argv0 = argv[0];
    app->options.persistSettings = true;

    std::string scanLibraryDir;
    std::string detectPath;
    bool sharedSaves = false;
    Kotonoha::CommandLine arguments("KotonohaLauncher", app->options);
    arguments.option("--library", "DIRECTORY|ISO",
                     "Multi-engine game library directory or individual game",
                     app->libraryPath);
    arguments.option("--scan-library", "DIRECTORY",
                     "Scan a library directory with Kotonoha multi-engine "
                     "detection and print the catalog",
                     scanLibraryDir);
    arguments.option("--detect", "PATH",
                     "Detect the engine family and CMake library for a single "
                     "game directory or disc image",
                     detectPath);
    arguments.option("--save-root", "DIRECTORY",
                     "Root directory for per-game isolated save profiles",
                     app->customSaveRoot);
    arguments.flag("--split-saves",
                   "Isolate save & config folders per game title (default)",
                   app->splitSavesByGame);
    arguments.flag("--shared-engine-saves",
                   "Share one save folder across all titles of the same engine",
                   sharedSaves);
    arguments.parse(argc, argv);
    if (sharedSaves)
      app->splitSavesByGame = false;
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    if (!detectPath.empty()) {
      const auto det =
          KotonohaLauncher::detectKotonohaEngineFromPath(detectPath);
      if (det.kind == Kotonoha::Filesystem::EngineKind::Unknown) {
        std::fprintf(stderr, "Unknown engine at: %s\n", detectPath.c_str());
        return SDL_APP_FAILURE;
      }
      const auto entry =
          KotonohaLauncher::buildDiscoveredGameEntry(detectPath, det);
      const std::string report = KotonohaLauncher::formatDiscoveredGamesReport(
          {entry}, app->splitSavesByGame);
      std::fputs(report.c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    if (!scanLibraryDir.empty()) {
      const auto discovered = KotonohaLauncher::scanGameLibrary(scanLibraryDir);
      const std::string report = KotonohaLauncher::formatDiscoveredGamesReport(
          discovered, app->splitSavesByGame);
      std::fputs(report.c_str(), stdout);
      return SDL_APP_SUCCESS;
    }

    app->frames = arguments.frames;
    if (app->options.persistSettings && app->options.settingsPath.empty())
      app->options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    const std::string settingsPath =
        app->options.persistSettings ? app->options.settingsPath : std::string();

    if (!app->options.launcher) {
      if (app->libraryPath.empty()) {
        throw std::runtime_error(
            "No game or library specified; pass --library DIRECTORY|ISO or "
            "run with --launcher");
      }
      const auto discovered =
          KotonohaLauncher::scanGameLibrary(app->libraryPath);
      if (discovered.empty()) {
        throw std::runtime_error("No supported game detected at: " +
                                 app->libraryPath);
      }
      const auto &chosen = discovered.front();
      if (app->startInProcess(chosen)) {
        *appstate = app.release();
        return SDL_APP_CONTINUE;
      }
      const int rc = KotonohaLauncher::launchModularPortProcess(
          app->argv0.c_str(), chosen, app->options, app->frames,
          app->splitSavesByGame, app->customSaveRoot);
      return rc == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    app->launcher = std::make_unique<KotonohaLauncher::UnifiedLauncher>(
        settingsPath, app->libraryPath, app->options.settings);
    app->launcher->setSplitSavesByGame(app->splitSavesByGame);
    if (!app->customSaveRoot.empty())
      app->launcher->setCustomSaveRoot(app->customSaveRoot);
    app->launcher->open();

    *appstate = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialization failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::event(void *appstate, SDL_Event *event) {
  try {
    auto &app = *static_cast<Application *>(appstate);
    if (app.launcher)
      return app.launcher->event(*event);
    return app.engine->Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  try {
    auto &app = *static_cast<Application *>(appstate);
    if (app.launcher) {
      const auto result = app.launcher->iterate();
      if (app.launcher->state() == Kotonoha::Launcher::State::Quit)
        return SDL_APP_SUCCESS;
      if (app.launcher->state() == Kotonoha::Launcher::State::Launch) {
        const auto chosen = app.launcher->selectedEntry();
        const std::string prevLibrary = app.launcher->libraryPath();
        app.options.settings = app.launcher->settings();
        app.splitSavesByGame = app.launcher->splitSavesByGame();
        app.customSaveRoot = app.launcher->customSaveRoot();
        app.launcher->close();
        app.launcher.reset();
        if (app.startInProcess(chosen))
          return result;
        const int rc = KotonohaLauncher::launchModularPortProcess(
            app.argv0.c_str(), chosen, app.options, app.frames,
            app.splitSavesByGame, app.customSaveRoot);
        if (app.frames > 0)
          return rc == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        // Reopen the unified launcher hub after the child game exits so the
        // user can immediately pick another game without restarting the hub.
        const std::string hubSettingsPath =
            app.options.persistSettings ? Kotonoha::EngineSettings::defaultPath()
                                        : std::string();
        app.launcher = std::make_unique<KotonohaLauncher::UnifiedLauncher>(
            hubSettingsPath, prevLibrary, app.options.settings);
        app.launcher->setSplitSavesByGame(app.splitSavesByGame);
        app.launcher->setCustomSaveRoot(app.customSaveRoot);
        app.launcher->open();
        return SDL_APP_CONTINUE;
      }
      return result;
    }
    const auto result = app.engine->Iterate();
    return app.frames > 0 && --app.frames == 0 ? SDL_APP_SUCCESS : result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<Application *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
