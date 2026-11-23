#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include "LargePonPonRuntime.hpp"
#include "PmailRuntime.hpp"

#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Pmail/GcPack.hpp>
#include <Pmail/LargePonPonAssets.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <SnowRadish/SnowAssets.hpp>

namespace {

struct Application {
  std::unique_ptr<Kotonoha::Runtime> game;
  Kotonoha::Engine engine;
  long framesRemaining = -1;
  ~Application() {
    if (auto *pm =
            dynamic_cast<Kotonoha::PmailPort::PmailRuntime *>(game.get()))
      pm->shutdown();
    else if (auto *lp = dynamic_cast<Kotonoha::PmailPort::LargePonPonRuntime *>(
                 game.get()))
      lp->shutdown();
  }

  Application(std::unique_ptr<Kotonoha::Runtime> runtime,
              Kotonoha::EngineOptions options)
      : game(std::move(runtime)), engine(*game, std::move(options)) {}
};

// Resolve packaged or user-provided game data independently of developer
// machine volumes. SnowAssets expects SCRIPT.DAT and GRAPHIC.DAT together.
std::string DefaultDirectory() {
  for (const char *relative :
       {"data/pack/SCRIPT.DAT", "data/pack/script.dat", "pack/SCRIPT.DAT",
        "pack/script.dat", "SCRIPT.DAT", "script.dat"}) {
    const auto found = Kotonoha::FileResolver::findGameAsset("pmail", relative);
    if (!found)
      continue;
    auto root = found->parent_path();
    for (const char *part = relative; *part; ++part)
      if (*part == '/')
        root = root.parent_path();
    return root.string();
  }
  return {};
}

bool validate(const std::string &path, std::string &info) {
  try {
    auto source = Kotonoha::Filesystem::Filesystem::mount(path);
    if (Kotonoha::Providers::isLargePonPonSource(*source)) {
      info = "Large PonPon script and graphics archives found";
      return true;
    }
    if (Kotonoha::Filesystem::findRequiredFilesPrefix(
            *source, {"SCRIPT.DAT", "GRAPHIC.DAT"},
            {"", "pack/", "data/pack/"})) {
      info = "Pure Mail script and graphics archives found";
      return true;
    }
    info = "Select a Pure Mail or Large PonPon source with SCRIPT.DAT and "
           "GRAPHIC.DAT";
    return false;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}

struct App {
  Kotonoha::EngineOptions options;
  long frames = -1;
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  std::unique_ptr<Application> game;

  void start(const std::string &directory) {
    if (Kotonoha::Providers::isLargePonPonDirectory(directory)) {
      auto assets =
          std::make_shared<Kotonoha::Providers::LargePonPonAssets>(directory);
      assets->setOverlay(options.assetOverlayDirectory());
      options.assets = assets;
      auto runtime = std::make_unique<Kotonoha::PmailPort::LargePonPonRuntime>(
          directory, std::move(assets));
      game = std::make_unique<Application>(std::move(runtime), options);
    } else {
      auto assets =
          std::make_shared<Kotonoha::Providers::SnowAssets>(directory);
      assets->setOverlay(options.assetOverlayDirectory());
      options.assets = assets;
      auto runtime = std::make_unique<Kotonoha::PmailPort::PmailRuntime>(
          directory, std::move(assets));
      game = std::make_unique<Application>(std::move(runtime), options);
    }
    game->framesRemaining = frames;
  }
};

} // namespace

// The port owns game-specific initialization and transitions. The shared
// SDLCallbackApp.hpp defines the four SDL callback entrypoints below.
struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **state, int argc, char **argv);
  static SDL_AppResult event(void *state, SDL_Event *event);
  static SDL_AppResult iterate(void *state);
  static void quit(void *state, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **appstate, int argc,
                                          char **argv) {
  *appstate = nullptr;
  KOTONOHA_SET_APP_METADATA("Pure Mail", "me.hirameki.kotonoha.pmail");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine arguments("Pmail", options);
    arguments.option("--pmail", "DIRECTORY|ISO",
                     "Pure Mail game directory or ISO image", directory);
    std::vector<char *> filteredArgv;
    filteredArgv.reserve(static_cast<std::size_t>(argc));
    if (argc > 0)
      filteredArgv.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i] ? argv[i] : "";
      if (arg == "--config" || arg == "--renderer" || arg == "--log" ||
          arg == "--mcp-root" || arg == "--mcp-port" ||
          arg == "--asset-overlay" || arg == "--dump-translation" ||
          arg == "--translation" || arg == "--subtitle-style" ||
          arg == "--ass-style" || arg == "--frames" || arg == "--pmail") {
        filteredArgv.push_back(argv[i]);
        if (i + 1 < argc)
          filteredArgv.push_back(argv[++i]);
      } else if (!arg.empty() && arg[0] != '-' && directory.empty()) {
        directory = arg;
      } else {
        filteredArgv.push_back(argv[i]);
      }
    }
    arguments.parse(static_cast<int>(filteredArgv.size()), filteredArgv.data());
    if (const char *vid = SDL_getenv("SDL_VIDEODRIVER");
        vid && std::string(vid) == "dummy") {
      options.persistSettings = false;
      options.settings.renderer = "software";
    }
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = arguments.frames;
    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    if (options.settingsPath.empty() && options.persistSettings)
      options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    const auto settingsPath =
        options.persistSettings ? options.settingsPath : std::string();
    const char *env = SDL_getenv("PMAIL_DIR");
    directory = Kotonoha::GameLauncher::findGamePath(
        "pmail",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         DefaultDirectory()},
        validate, {"", "pack", "data/pack"}, true);
    // Honor the shared default and explicit --no-launcher. Automated
    // --frames and MCP runs bypass the launcher in CommandLine::parse().
    if (directory.empty() && !options.launcher)
      throw std::runtime_error(
          "No valid Pure Mail source; pass --pmail DIRECTORY|ISO");
    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "Pure Mail", settingsPath, directory, validate, options.settings,
          true);
      app->launcher->setAudioChannels({"BGM", "Voice", "Se"});
      app->launcher->open();
    } else {
      app->start(directory);
    }
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
    auto &app = *static_cast<App *>(appstate);
    return app.launcher ? app.launcher->event(*event)
                        : app.game->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  try {
    auto &app = *static_cast<App *>(appstate);
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
    if (result != SDL_APP_CONTINUE)
      return result;
    return app.frames > 0 && --app.frames == 0 ? SDL_APP_SUCCESS
                                               : SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void KotonohaPortCallbacks::quit(void *appstate, SDL_AppResult) {
  delete static_cast<App *>(appstate);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
