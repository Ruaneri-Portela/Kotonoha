// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include <KotonohaLauncher/UnifiedLauncher.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

// A game uses its own save root for its entire in-process lifetime. Restore
// the hub's environment before opening the next game or exiting.
struct ScopedGameUserData {
  std::optional<std::string> previousSdl;
#if defined(_WIN32)
  std::optional<std::wstring> previousNative;
#else
  std::optional<std::string> previousNative;
#endif

  ScopedGameUserData() {
    if (const char *value = SDL_getenv("KOTONOHA_USER_DATA_DIR"))
      previousSdl = value;
#if defined(_WIN32)
    if (const wchar_t *value = _wgetenv(L"KOTONOHA_USER_DATA_DIR"))
      previousNative = value;
#else
    if (const char *value = std::getenv("KOTONOHA_USER_DATA_DIR"))
      previousNative = value;
#endif
  }

  ScopedGameUserData(const ScopedGameUserData &) = delete;
  ScopedGameUserData &operator=(const ScopedGameUserData &) = delete;
  ~ScopedGameUserData() {
    if (auto *env = SDL_GetEnvironment()) {
      if (previousSdl)
        SDL_SetEnvironmentVariable(env, "KOTONOHA_USER_DATA_DIR",
                                   previousSdl->c_str(), true);
      else
        SDL_UnsetEnvironmentVariable(env, "KOTONOHA_USER_DATA_DIR");
    }
#if defined(_WIN32)
    _wputenv_s(L"KOTONOHA_USER_DATA_DIR",
               previousNative ? previousNative->c_str() : L"");
#else
    if (previousNative)
      setenv("KOTONOHA_USER_DATA_DIR", previousNative->c_str(), 1);
    else
      unsetenv("KOTONOHA_USER_DATA_DIR");
#endif
  }
};

struct Application {
  Kotonoha::EngineOptions options;
  Kotonoha::EngineOptions gameOptions;
  std::string libraryPath;
  std::string customSaveRoot;
  bool splitSavesByGame = true;
  long frames = -1;
  bool returnToHub = false;

  std::unique_ptr<KotonohaLauncher::UnifiedLauncher> launcher;
  std::unique_ptr<ScopedGameUserData> gameEnvironment;
  std::unique_ptr<Kotonoha::Runtime> runtime;
  std::shared_ptr<Kotonoha::AssetProvider> assets;
  std::function<void(Kotonoha::Runtime &)> shutdownRuntime;
  std::unique_ptr<Kotonoha::Engine> engine;

  ~Application() { stopGame(); }

  bool startInProcess(const KotonohaLauncher::DiscoveredGameEntry &entry) {
    auto &registry = KotonohaLauncher::EngineLibraryRegistry::instance();
    if (!registry.canInstantiateInProcess(entry.engine.kind))
      return false;

    gameOptions = options;
    gameOptions.launcher = false;
    if (!entry.overlayPath.empty() && !gameOptions.assetOverlaySpecified) {
      gameOptions.assetOverlayPath = entry.overlayPath;
      gameOptions.assetOverlaySpecified = true;
    }

    gameEnvironment = std::make_unique<ScopedGameUserData>();
    KotonohaLauncher::applyPerGameSaveIsolation(
        entry, gameOptions, splitSavesByGame, customSaveRoot);
#if defined(_WIN32)
    // The CRT and SDL have separate environment storage on Windows.
    // Preserve Unicode profile paths for runtimes using std::filesystem.
    const std::wstring nativeRoot = std::filesystem::u8path(
        KotonohaLauncher::resolveIsolatedUserDataRoot(
            entry, splitSavesByGame, customSaveRoot)).wstring();
    if (_wputenv_s(L"KOTONOHA_USER_DATA_DIR", nativeRoot.c_str()) != 0)
      throw std::runtime_error("Unable to set the game save environment");
#endif

    auto mounted = registry.instantiate(entry, gameOptions);
    if (!mounted.runtime) {
      gameEnvironment.reset();
      return false;
    }
    runtime = std::move(mounted.runtime);
    assets = std::move(mounted.assets);
    shutdownRuntime = std::move(mounted.shutdown);
    if (assets)
      gameOptions.assets = assets;
    engine = std::make_unique<Kotonoha::Engine>(*runtime, gameOptions);
    returnToHub = false;
    return true;
  }

  void stopGame() {
    if (runtime && shutdownRuntime) {
      try {
        shutdownRuntime(*runtime);
      } catch (const std::exception &e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Game runtime shutdown failed: %s", e.what());
      }
    }
    // The engine owns the renderer. Destroy it before the game runtime and
    // the provider, but after the runtime has released its textures/audio.
    engine.reset();
    runtime.reset();
    assets.reset();
    gameOptions.assets.reset();
    shutdownRuntime = {};
    gameEnvironment.reset();
    returnToHub = false;
  }

  void openHub() {
    // Engine initialization may set game-specific SDL application metadata.
    // Restore the hub identity before reopening its settings/UI.
    KOTONOHA_SET_APP_METADATA("KotonohaLauncher",
                              "me.hirameki.kotonoha.launcher");
    launcher = std::make_unique<KotonohaLauncher::UnifiedLauncher>(
        options.persistSettings ? options.settingsPath : std::string{},
        libraryPath, options.settings);
    launcher->setSplitSavesByGame(splitSavesByGame);
    launcher->setCustomSaveRoot(customSaveRoot);
    launcher->open();
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
    app->options.persistSettings = true;

    std::string scanLibraryDir;
    std::string detectPath;
    bool sharedSaves = false;
    Kotonoha::CommandLine args("KotonohaLauncher", app->options);
    args.option("--library", "DIRECTORY|ISO",
                "Game library directory or individual game", app->libraryPath);
    args.option("--scan-library", "DIRECTORY",
                "Detect games and print the engine/linked-library catalog",
                scanLibraryDir);
    args.option("--detect", "PATH",
                "Detect a game engine from a folder or disc image", detectPath);
    args.option("--save-root", "DIRECTORY",
                "Root directory for isolated game saves", app->customSaveRoot);
    args.flag("--split-saves", "Isolate saves by game title (default)",
              app->splitSavesByGame);
    args.flag("--shared-engine-saves", "Share saves across the same engine",
              sharedSaves);
    args.parse(argc, argv);
    if (sharedSaves)
      app->splitSavesByGame = false;
    if (args.wantsHelp()) {
      std::fputs(args.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }

    if (!detectPath.empty()) {
      const auto detected =
          KotonohaLauncher::detectKotonohaEngineFromPath(detectPath);
      if (detected.kind == Kotonoha::Filesystem::EngineKind::Unknown) {
        std::fprintf(stderr, "Unknown engine at: %s\n", detectPath.c_str());
        return SDL_APP_FAILURE;
      }
      const auto entry =
          KotonohaLauncher::buildDiscoveredGameEntry(detectPath, detected);
      std::fputs(KotonohaLauncher::formatDiscoveredGamesReport(
                     {entry}, app->splitSavesByGame).c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    if (!scanLibraryDir.empty()) {
      std::fputs(KotonohaLauncher::formatDiscoveredGamesReport(
                     KotonohaLauncher::scanGameLibrary(scanLibraryDir),
                     app->splitSavesByGame).c_str(), stdout);
      return SDL_APP_SUCCESS;
    }

    app->frames = args.frames;
    if (app->options.persistSettings && app->options.settingsPath.empty())
      app->options.settingsPath = Kotonoha::EngineSettings::defaultPath();

    if (!app->options.launcher) {
      if (app->libraryPath.empty())
        throw std::runtime_error(
            "No game selected; pass --library DIRECTORY|ISO");
      const auto games = KotonohaLauncher::scanGameLibrary(app->libraryPath);
      if (games.empty())
        throw std::runtime_error("No supported game found in: " +
                                 app->libraryPath);
      if (!app->startInProcess(games.front()))
        throw std::runtime_error(
            "The selected game is not linked into this launcher; enable "
            "its KOTONOHA_BUILD_<PORT> option or KOTONOHA_BUILD_ALL");
    } else {
      app->openHub();
    }

    *appstate = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Launcher initialization failed: %s", error.what());
    // A double-clicked macOS .app or Windows executable may have no visible
    // terminal; show the actual SDL/configuration error instead of just exiting.
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "KotonohaLauncher - initialization failed",
                             error.what(), nullptr);
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::event(void *state, SDL_Event *event) {
  try {
    auto &app = *static_cast<Application *>(state);
    if (app.launcher)
      return app.launcher->event(*event);
    if (!app.engine)
      return SDL_APP_CONTINUE;
    const auto result = app.engine->Event(event);
    if (result == SDL_APP_SUCCESS && app.frames <= 0) {
      app.returnToHub = true;
      return SDL_APP_CONTINUE;
    }
    return result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *state) {
  try {
    auto &app = *static_cast<Application *>(state);
    if (app.launcher) {
      const auto result = app.launcher->iterate();
      if (app.launcher->state() == Kotonoha::Launcher::State::Quit)
        return SDL_APP_SUCCESS;
      if (app.launcher->state() == Kotonoha::Launcher::State::Launch) {
        const auto choice = app.launcher->selectedEntry();
        app.libraryPath = app.launcher->libraryPath();
        app.options.settings = app.launcher->settings();
        app.splitSavesByGame = app.launcher->splitSavesByGame();
        app.customSaveRoot = app.launcher->customSaveRoot();
        app.launcher->close();
        app.launcher.reset();
        if (!app.startInProcess(choice))
          throw std::runtime_error("Selected game runtime is not linked");
        return SDL_APP_CONTINUE;
      }
      return result;
    }

    if (!app.engine)
      return SDL_APP_FAILURE;
    if (app.returnToHub) {
      app.stopGame();
      app.openHub();
      return SDL_APP_CONTINUE;
    }

    const SDL_AppResult result = app.engine->Iterate();
    if (app.frames > 0 && --app.frames == 0)
      return SDL_APP_SUCCESS;
    if (result == SDL_APP_SUCCESS) {
      app.stopGame();
      app.openHub();
      return SDL_APP_CONTINUE;
    }
    return result;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *state, SDL_AppResult) {
  delete static_cast<Application *>(state);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
