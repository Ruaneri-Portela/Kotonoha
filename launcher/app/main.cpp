// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <KotonohaLauncher/LauncherBridge.h>
#include <KotonohaLauncher/UnifiedLauncher.hpp>
#if defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
#include "macos/KotonohaMacLauncherBridge.h"
#endif

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
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

struct PendingNativeLaunch {
  std::string gamePath;
  std::string engineSlug;
  std::string saveProfileId;
  bool splitSavesByGame = true;
  std::string customSaveRoot;
  KotonohaLauncher::GameLaunchParameters params;
};

PendingNativeLaunch fromLaunchRequest(const KotonohaGameLaunchRequest &req) {
  PendingNativeLaunch launch;
  launch.gamePath = req.gamePath ? req.gamePath : "";
  launch.engineSlug = req.engineSlug ? req.engineSlug : "";
  launch.saveProfileId = req.saveProfileId ? req.saveProfileId : "";
  launch.splitSavesByGame = req.splitSavesByGame != 0;
  launch.customSaveRoot = req.customSaveRoot ? req.customSaveRoot : "";
  launch.params.assetOverlayPath =
      req.assetOverlayPath ? req.assetOverlayPath : "";
  launch.params.translationPath =
      req.translationPath ? req.translationPath : "";
  launch.params.subtitleStylePath =
      req.subtitleStylePath ? req.subtitleStylePath : "";
  launch.params.customDocumentsDir =
      req.customDocumentsDir ? req.customDocumentsDir : "";
  launch.params.ktrfPath = req.ktrfPath ? req.ktrfPath : "";
  launch.params.scriptsDir = req.scriptsDir ? req.scriptsDir : "";
  launch.params.startScenario = req.startScenario ? req.startScenario : "";
  launch.params.rendererBackend =
      req.rendererBackend ? req.rendererBackend : "";
  launch.params.upscalerBackend =
      req.upscalerBackend ? req.upscalerBackend : "";
  launch.params.upscalerPreset = req.upscalerPreset ? req.upscalerPreset : "";
  launch.params.upscalerFactor = req.upscalerFactor ? req.upscalerFactor : "";
  return launch;
}

void applyGraphicsParams(const KotonohaLauncher::GameLaunchParameters &params,
                         Kotonoha::EngineOptions &gameOptions) {
  if (!params.rendererBackend.empty() && gameOptions.rendererOverride.empty()) {
    gameOptions.rendererOverride = params.rendererBackend;
  }
  if (!params.upscalerBackend.empty() &&
      !gameOptions.upscalerBackendOverride.has_value()) {
    std::string key = params.upscalerBackend;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    if (key == "none" || key == "off" || key == "0")
      gameOptions.upscalerBackendOverride = Kotonoha::UpscaleBackend::None;
    else if (key == "linear" || key == "1")
      gameOptions.upscalerBackendOverride = Kotonoha::UpscaleBackend::Linear;
    else if (key == "mpv" || key == "opengl" || key == "2")
      gameOptions.upscalerBackendOverride = Kotonoha::UpscaleBackend::MPV;
    else if (key == "metalfx" || key == "3")
      gameOptions.upscalerBackendOverride = Kotonoha::UpscaleBackend::MetalFX;
    else if (key == "auto" || key == "automatic" || key == "4")
      gameOptions.upscalerBackendOverride = Kotonoha::UpscaleBackend::Auto;
  }
  if (!params.upscalerPreset.empty() &&
      !gameOptions.upscalerPresetOverride.has_value()) {
    int idx = -1;
    const auto parsed = std::from_chars(
        params.upscalerPreset.data(),
        params.upscalerPreset.data() + params.upscalerPreset.size(), idx);
    const auto &presets = Kotonoha::UpscalePresets();
    if (parsed.ec == std::errc{} &&
        parsed.ptr ==
            params.upscalerPreset.data() + params.upscalerPreset.size() &&
        idx >= 0 && idx < static_cast<int>(presets.size())) {
      gameOptions.upscalerPresetOverride =
          presets[static_cast<std::size_t>(idx)];
    } else {
      std::string key = params.upscalerPreset;
      std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
      for (auto preset : presets) {
        std::string name = Kotonoha::UpscalePresetName(preset);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) {
                         return static_cast<char>(std::tolower(ch));
                       });
        if (name == key) {
          gameOptions.upscalerPresetOverride = preset;
          break;
        }
      }
    }
  }
  if (!params.upscalerFactor.empty() &&
      !gameOptions.upscalerFactorOverride.has_value()) {
    char *end = nullptr;
    const float factor = std::strtof(params.upscalerFactor.c_str(), &end);
    if (end && *end == '\0' && std::isfinite(factor) && factor >= 1.0f &&
        factor <= 4.0f) {
      gameOptions.upscalerFactorOverride = factor;
    }
  }
}

struct Application {
  Kotonoha::EngineOptions options;
  Kotonoha::EngineOptions gameOptions;
  std::string libraryPath;
  std::string customSaveRoot;
  bool splitSavesByGame = true;
  long frames = -1;
  bool returnToHub = false;
  bool launchedFromNativeAndroidHub = false;

  std::unique_ptr<KotonohaLauncher::UnifiedLauncher> launcher;
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  Kotonoha::WindowPtr iosHubWindow;
  bool iosNativeHubActive = false;
  std::mutex iosLaunchMutex;
  std::optional<PendingNativeLaunch> pendingIOSLaunch;
  bool pendingIOSQuit = false;
#endif
#if defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
  Kotonoha::WindowPtr macHubWindow;
  bool macNativeHubActive = false;
  std::mutex macLaunchMutex;
  std::optional<PendingNativeLaunch> pendingMacLaunch;
  bool pendingMacQuit = false;
#endif
  std::unique_ptr<ScopedGameUserData> gameEnvironment;
  std::unique_ptr<Kotonoha::Runtime> runtime;
  std::string activeGame;
  std::shared_ptr<Kotonoha::AssetProvider> assets;
  std::function<void(Kotonoha::Runtime &)> shutdownRuntime;
  std::unique_ptr<Kotonoha::Engine> engine;

  ~Application() {
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    if (iosNativeHubActive) {
      KotonohaIOSDismissNativeLauncher();
      iosNativeHubActive = false;
    }
    iosHubWindow.reset();
#endif
#if defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
    if (macNativeHubActive)
      KotonohaMacDismissNativeLauncher();
    macHubWindow.reset();
#endif
    stopGame();
  }

  bool startInProcess(const KotonohaLauncher::DiscoveredGameEntry &entry) {
    const char *phase = "registry lookup";
    try {
      auto &registry = KotonohaLauncher::EngineLibraryRegistry::instance();
      if (!registry.canInstantiateInProcess(entry.engine.kind))
        return false;

      activeGame = entry.engine.slug + " (" + entry.path + ")";

      gameOptions = options;
      gameOptions.launcher = false;
      if (!entry.params.assetOverlayPath.empty()) {
        gameOptions.assetOverlayPath = entry.params.assetOverlayPath;
        gameOptions.assetOverlaySpecified = true;
      } else if (!entry.overlayPath.empty() &&
                 !gameOptions.assetOverlaySpecified) {
        gameOptions.assetOverlayPath = entry.overlayPath;
        gameOptions.assetOverlaySpecified = true;
      }
      if (!entry.params.translationPath.empty() &&
          gameOptions.translationPath.empty()) {
        gameOptions.translationPath = entry.params.translationPath;
      }
      if (!entry.params.subtitleStylePath.empty() &&
          gameOptions.subtitleStylePath.empty()) {
        gameOptions.subtitleStylePath = entry.params.subtitleStylePath;
      }
      applyGraphicsParams(entry.params, gameOptions);

      gameEnvironment = std::make_unique<ScopedGameUserData>();
      const std::string effectiveSaveRoot =
          !entry.params.customDocumentsDir.empty()
              ? entry.params.customDocumentsDir
              : customSaveRoot;
      KotonohaLauncher::applyPerGameSaveIsolation(
          entry, gameOptions, splitSavesByGame, effectiveSaveRoot);
#if defined(_WIN32)
      // The CRT and SDL have separate environment storage on Windows.
      // Preserve Unicode profile paths for runtimes using std::filesystem.
      const std::string utf8Root =
          KotonohaLauncher::resolveIsolatedUserDataRoot(
              entry, splitSavesByGame, effectiveSaveRoot);
      const int wideLength =
          MultiByteToWideChar(CP_UTF8, 0, utf8Root.c_str(), -1, nullptr, 0);
      std::wstring nativeRoot(wideLength > 0 ? wideLength - 1 : 0, L'\0');
      if (wideLength > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8Root.c_str(), -1,
                            nativeRoot.data(), wideLength);
      if (_wputenv_s(L"KOTONOHA_USER_DATA_DIR", nativeRoot.c_str()) != 0)
        throw std::runtime_error("Unable to set the game save environment");
#endif

      phase = "game assets and runtime construction";
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
      phase = "engine construction";
      engine = std::make_unique<Kotonoha::Engine>(*runtime, gameOptions);
      if (!entry.params.startScenario.empty()) {
        try {
          runtime->SetState(Kotonoha::GameState::Gameplay);
          engine->debugJumpToNode(entry.params.startScenario);
        } catch (const std::exception &e) {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "Failed to jump to startScenario '%s': %s",
                      entry.params.startScenario.c_str(), e.what());
        }
      }
      returnToHub = false;
      return true;
    } catch (const std::bad_alloc &) {
      throw std::runtime_error(std::string("Memory allocation failed during ") +
                               phase + " for " + entry.path);
    }
  }

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) &&                       \
    !(defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
  // Desktop macOS/Linux launches run in a disowned child process created by
  // KotonohaLauncherCore. The core owns the platform mechanism; this app only
  // supplies the request and keeps its native hub responsive.
  // Launches using the request assembled by the SwiftUI hub and returns the
  // PID of the detached host so the hub can show "Running" and an End button.
  int detachRequest(const KotonohaGameLaunchRequest &request) {
    char *result = KotonohaLauncherLaunchGameDetached(&request);
    if (!result)
      return 0;
    const std::string text(result);
    KotonohaLauncherFreeString(result);
    const auto pos = text.find("\"pid\":");
    if (pos == std::string::npos)
      return 0;
    int pid = 0;
    const auto parsed = std::from_chars(
        text.data() + pos + 6, text.data() + text.size(), pid);
    return parsed.ec == std::errc() ? pid : 0;
  }

  bool startDetached(const KotonohaLauncher::DiscoveredGameEntry &entry) {
    KotonohaGameLaunchRequest request{};
    request.gamePath = entry.path.c_str();
    request.engineSlug = entry.engine.slug.c_str();
    request.saveProfileId = entry.saveProfileId.c_str();
    request.splitSavesByGame = splitSavesByGame ? 1 : 0;
    request.customSaveRoot = customSaveRoot.c_str();
    request.assetOverlayPath = entry.params.assetOverlayPath.c_str();
    request.translationPath = entry.params.translationPath.c_str();
    request.subtitleStylePath = entry.params.subtitleStylePath.c_str();
    request.customDocumentsDir = entry.params.customDocumentsDir.c_str();
    request.ktrfPath = entry.params.ktrfPath.c_str();
    request.scriptsDir = entry.params.scriptsDir.c_str();
    request.startScenario = entry.params.startScenario.c_str();
    request.rendererBackend = entry.params.rendererBackend.c_str();
    request.upscalerBackend = entry.params.upscalerBackend.c_str();
    request.upscalerPreset = entry.params.upscalerPreset.c_str();
    request.upscalerFactor = entry.params.upscalerFactor.c_str();
    char *result = KotonohaLauncherLaunchGameDetached(&request);
    const bool ok = result &&
                    std::string(result).find("\"ok\":true") != std::string::npos;
    if (result)
      KotonohaLauncherFreeString(result);
    return ok;
  }
#endif

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

#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  static void onIOSNativeLaunch(const KotonohaGameLaunchRequest *req,
                                void *userData) {
    if (!req || !userData)
      return;
    auto *self = static_cast<Application *>(userData);
    PendingNativeLaunch launch = fromLaunchRequest(*req);
    std::lock_guard<std::mutex> lock(self->iosLaunchMutex);
    self->pendingIOSLaunch = std::move(launch);
  }

  static void onIOSNativeQuit(void *userData) {
    if (!userData)
      return;
    auto *self = static_cast<Application *>(userData);
    std::lock_guard<std::mutex> lock(self->iosLaunchMutex);
    self->pendingIOSQuit = true;
  }
#endif

#if defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
  static int onMacNativeLaunch(const KotonohaGameLaunchRequest *req,
                               void *userData) {
    auto *self = static_cast<Application *>(userData);
    if (!self || !req)
      return 0;
    return self->detachRequest(*req);
  }

  static void onMacNativeQuit(void *userData) {
    auto *self = static_cast<Application *>(userData);
    if (!self)
      return;
    std::lock_guard<std::mutex> lock(self->macLaunchMutex);
    self->pendingMacQuit = true;
  }
#endif

  void openHub() {
    // Engine initialization may set game-specific SDL application metadata.
    // Restore the hub identity before reopening its settings/UI.
    KOTONOHA_SET_APP_METADATA("Kotonoha",
                              "me.hirameki.kotonoha");
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    if (!iosHubWindow) {
      if (!SDL_WasInit(SDL_INIT_VIDEO))
        SDL_InitSubSystem(SDL_INIT_VIDEO);
      iosHubWindow.reset(SDL_CreateWindow("Kotonoha", 1280, 720,
                                          SDL_WINDOW_FULLSCREEN |
                                              SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_HIGH_PIXEL_DENSITY));
    }
    iosNativeHubActive = true;
    KotonohaIOSPresentNativeLauncher(&Application::onIOSNativeLaunch,
                                     &Application::onIOSNativeQuit, this);
#elif defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
    if (!macHubWindow) {
      if (!SDL_WasInit(SDL_INIT_VIDEO))
        SDL_InitSubSystem(SDL_INIT_VIDEO);
      macHubWindow.reset(
          SDL_CreateWindow("Kotonoha", 1, 1, SDL_WINDOW_HIDDEN));
    }
    macNativeHubActive = true;
    KotonohaMacPresentNativeLauncher(&Application::onMacNativeLaunch,
                                     &Application::onMacNativeQuit, this);
#else
    launcher = std::make_unique<KotonohaLauncher::UnifiedLauncher>(
        options.persistSettings ? options.settingsPath : std::string{},
        libraryPath, options.settings);
    launcher->setSplitSavesByGame(splitSavesByGame);
    launcher->setCustomSaveRoot(customSaveRoot);
    launcher->open();
#endif
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
  Kotonoha::DisableMetalDebugEnvironment();
  KOTONOHA_SET_APP_METADATA("Kotonoha",
                            "me.hirameki.kotonoha");
  try {
    auto app = std::make_unique<Application>();
    app->options.persistSettings = true;

    std::string scanLibraryDir;
    std::string detectPath;
    KotonohaLauncher::GameLaunchParameters cliParams;
    bool sharedSaves = false;
    Kotonoha::CommandLine args("Kotonoha", app->options);
    args.option("--library", "DIRECTORY|ISO",
                "Game library directory or individual game", app->libraryPath);
    args.option("--scan-library", "DIRECTORY",
                "Detect games and print the engine/linked-library catalog",
                scanLibraryDir);
    args.option("--detect", "PATH",
                "Detect a game engine from a folder or disc image", detectPath);
    args.option("--save-root", "DIRECTORY",
                "Root directory for isolated game saves", app->customSaveRoot);
    args.option("--ktrf", "FILE",
                "Optional School Days routing graph (.ktnroute / .ktrf)",
                cliParams.ktrfPath);
    args.option("--scripts", "DIRECTORY",
                "Optional School Days translated ORS script directory",
                cliParams.scriptsDir);
    args.option("--documents", "DIRECTORY",
                "Optional custom save/documents directory",
                cliParams.customDocumentsDir);
    args.option("--start", "ID",
                "Optional initial scenario or node ID override",
                cliParams.startScenario);
    args.alias("--start", "--node");
    args.flag("--split-saves", "Isolate saves by game title (default)",
              app->splitSavesByGame);
    args.flag("--shared-engine-saves", "Share saves across the same engine",
              sharedSaves);
    args.parse(argc, argv);
    if (app->libraryPath.rfind("documents:", 0) == 0) {
      const auto relative = app->libraryPath.substr(10);
      if (relative.empty() || relative.front() == '/' ||
          relative.find("..") != std::string::npos)
        throw std::invalid_argument("Invalid documents: game path");
      app->libraryPath = (Kotonoha::documentsDirectory() / relative).string();
    }
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
                     {entry}, app->splitSavesByGame)
                     .c_str(),
                 stdout);
      return SDL_APP_SUCCESS;
    }
    if (!scanLibraryDir.empty()) {
      std::fputs(KotonohaLauncher::formatDiscoveredGamesReport(
                     KotonohaLauncher::scanGameLibrary(scanLibraryDir),
                     app->splitSavesByGame)
                     .c_str(),
                 stdout);
      return SDL_APP_SUCCESS;
    }

    app->frames = args.frames;
    if (app->options.persistSettings && app->options.settingsPath.empty())
      app->options.settingsPath = Kotonoha::EngineSettings::defaultPath();

#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    // Direct --library launches are useful for device diagnostics; normal
    // launches still start in the native SwiftUI hub.
    if (app->libraryPath.empty())
      app->options.launcher = true;
#elif defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
    // The Xcode macOS launcher uses the native SwiftUI hub.
    if (app->libraryPath.empty())
      app->options.launcher = true;
#elif defined(__ANDROID__)
    // Android starts in GameActivity only when launched with --no-launcher
    // --library <path> from the native Material 3 LauncherActivity.
    if (app->libraryPath.empty()) {
      app->options.launcher = true;
    } else if (!app->options.launcher) {
      app->launchedFromNativeAndroidHub = true;
    }
#endif
    if (!app->options.launcher) {
      if (app->libraryPath.empty())
        throw std::runtime_error(
            "No game selected; pass --library DIRECTORY|ISO");
      const auto games = KotonohaLauncher::scanGameLibrary(app->libraryPath);
      if (games.empty())
        throw std::runtime_error("No supported game found in: " +
                                 app->libraryPath);
      auto targetEntry = games.front();
      if (!cliParams.ktrfPath.empty())
        targetEntry.params.ktrfPath = cliParams.ktrfPath;
      if (!cliParams.scriptsDir.empty())
        targetEntry.params.scriptsDir = cliParams.scriptsDir;
      if (!cliParams.customDocumentsDir.empty())
        targetEntry.params.customDocumentsDir = cliParams.customDocumentsDir;
      if (!cliParams.startScenario.empty())
        targetEntry.params.startScenario = cliParams.startScenario;
      if (!app->startInProcess(targetEntry))
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
    // terminal; show the actual SDL/configuration error instead of just
    // exiting.
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "Kotonoha - initialization failed",
                             error.what(), nullptr);
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::event(void *state, SDL_Event *event) {
  try {
    auto &app = *static_cast<Application *>(state);
#if defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
    if (app.macNativeHubActive)
      return SDL_APP_CONTINUE;
#endif
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    if (app.iosNativeHubActive) {
      if (event && event->type == SDL_EVENT_DROP_FILE && event->drop.data) {
        KotonohaIOSNotifyOpenedURL(event->drop.data);
      }
      return SDL_APP_CONTINUE;
    }
#endif
    if (app.launcher)
      return app.launcher->event(*event);
    if (!app.engine)
      return SDL_APP_CONTINUE;
    const auto result = app.engine->Event(event);
    if (result == SDL_APP_SUCCESS && app.frames <= 0) {
      if (app.launchedFromNativeAndroidHub)
        return SDL_APP_SUCCESS;
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
#if defined(KOTONOHA_MAC_NATIVE_LAUNCHER)
    if (app.macNativeHubActive) {
      std::optional<PendingNativeLaunch> pending;
      bool shouldQuit = false;
      {
        std::lock_guard<std::mutex> lock(app.macLaunchMutex);
        pending = std::move(app.pendingMacLaunch);
        app.pendingMacLaunch.reset();
        shouldQuit = app.pendingMacQuit;
        app.pendingMacQuit = false;
      }
      if (shouldQuit) {
        KotonohaMacDismissNativeLauncher();
        app.macNativeHubActive = false;
        app.macHubWindow.reset();
        return SDL_APP_SUCCESS;
      }
      if (pending) {
        const auto detected =
            KotonohaLauncher::detectKotonohaEngineFromPath(pending->gamePath);
        if (detected.kind == Kotonoha::Filesystem::EngineKind::Unknown) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                       "Could not detect engine for: %s",
                       pending->gamePath.c_str());
          return SDL_APP_CONTINUE;
        }
        auto entry = KotonohaLauncher::buildDiscoveredGameEntry(
            pending->gamePath, detected);
        if (!pending->saveProfileId.empty())
          entry.saveProfileId = pending->saveProfileId;
        entry.params = pending->params;
        app.splitSavesByGame = pending->splitSavesByGame;
        app.customSaveRoot = pending->customSaveRoot;
        if (!app.startDetached(entry))
          throw std::runtime_error("Unable to start the detached game host");
        return SDL_APP_CONTINUE;
      }
      SDL_Delay(8);
      return SDL_APP_CONTINUE;
    }
#endif
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    if (app.iosNativeHubActive) {
      std::optional<PendingNativeLaunch> pending;
      bool shouldQuit = false;
      {
        std::lock_guard<std::mutex> lock(app.iosLaunchMutex);
        pending = std::move(app.pendingIOSLaunch);
        app.pendingIOSLaunch.reset();
        shouldQuit = app.pendingIOSQuit;
      }
      if (shouldQuit) {
        KotonohaIOSDismissNativeLauncher();
        app.iosNativeHubActive = false;
        app.iosHubWindow.reset();
        return SDL_APP_SUCCESS;
      }
      if (pending) {
        KotonohaIOSDismissNativeLauncher();
        app.iosNativeHubActive = false;
        app.iosHubWindow.reset();

        const auto detected =
            KotonohaLauncher::detectKotonohaEngineFromPath(pending->gamePath);
        if (detected.kind == Kotonoha::Filesystem::EngineKind::Unknown) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                       "Could not detect engine for: %s",
                       pending->gamePath.c_str());
          app.openHub();
          KotonohaIOSPresentError(
              "Unable to Launch Game",
              ("Could not detect a supported engine in:\n" + pending->gamePath)
                  .c_str());
          return SDL_APP_CONTINUE;
        }
        auto entry = KotonohaLauncher::buildDiscoveredGameEntry(
            pending->gamePath, detected);
        if (!pending->saveProfileId.empty())
          entry.saveProfileId = pending->saveProfileId;
        entry.params = pending->params;
        app.splitSavesByGame = pending->splitSavesByGame;
        app.customSaveRoot = pending->customSaveRoot;
        try {
          if (!app.startInProcess(entry)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Selected game runtime is not linked: %s",
                         entry.engine.slug.c_str());
            app.openHub();
            KotonohaIOSPresentError(
                "Engine Not Linked",
                ("Runtime is not linked in this build: " + entry.engine.slug)
                    .c_str());
          }
        } catch (const std::exception &error) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                       "Failed to launch game [%s]: %s",
                       entry.engine.slug.c_str(), error.what());
          app.stopGame();
          app.openHub();
          KotonohaIOSPresentError("Failed to Launch Game", error.what());
        }
        return SDL_APP_CONTINUE;
      }
      SDL_Delay(16);
      return SDL_APP_CONTINUE;
    }
#endif
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
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) &&                       \
    !(defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        const bool started = app.startDetached(choice);
#else
        const bool started = app.startInProcess(choice);
#endif
        app.launcher->close();
        app.launcher.reset();
        if (!started)
          throw std::runtime_error("Unable to start the detached game host");
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
      if (app.launchedFromNativeAndroidHub)
        return SDL_APP_SUCCESS;
      app.openHub();
      return SDL_APP_CONTINUE;
    }
    return result;
  } catch (const std::exception &error) {
    auto &app = *static_cast<Application *>(state);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game frame failed [%s]: %s",
                 app.activeGame.empty() ? "launcher" : app.activeGame.c_str(),
                 error.what());
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    if (app.frames <= 0) {
      app.stopGame();
      app.openHub();
      KotonohaIOSPresentError("Game Error", error.what());
      return SDL_APP_CONTINUE;
    }
#else
    if (app.frames <= 0) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                               "Kotonoha - runtime error", error.what(),
                               nullptr);
    }
#endif
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *state, SDL_AppResult) {
  delete static_cast<Application *>(state);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
