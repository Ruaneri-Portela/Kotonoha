#include <Kirikiri/KirikiriAssets.hpp>
#include <Kirikiri/KirikiriRuntime.hpp>
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

Kotonoha::EngineOptions
withAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Kotonoha::Providers::KirikiriAssets> assets) {
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
    Game(std::shared_ptr<Kotonoha::Providers::KirikiriAssets> a,
         Kotonoha::EngineOptions o)
        : runtime(a), engine(runtime, withAssets(std::move(o), std::move(a))) {}
  };
  std::unique_ptr<Game> game;

  void start(const std::string &directory) {
    auto assets = std::shared_ptr<Kotonoha::Providers::KirikiriAssets>(
        new Kotonoha::Providers::KirikiriAssets(
            directory, options.assetOverlayDirectory()));
    // Archive precedence is indexed from the virtual mount before gameplay.
    assets->mountAllXp3();
    game = std::make_unique<Game>(std::move(assets), options);
  }
};

bool validate(const std::string &path, std::string &info) {
  try {
    auto fs = Kotonoha::Filesystem::Filesystem::mount(path);
    // Manifest-free HTTP cannot enumerate an undiscovered XP3 or script.
    // Recognize common entrypoints with direct VFS HEAD/Range probes first.
    for (const char *marker :
         {"data.xp3", "patch.xp3", "startup.tjs", "startup.ks",
          "scenario/startup.ks", "scenario/first.ks", "first.ks",
          "data/data.xp3"}) {
      if (Kotonoha::Filesystem::isGameSourceRegularFile(*fs, marker)) {
        info = std::string("Kirikiri virtual source detected: ") + marker;
        return true;
      }
    }
    bool scenario = false, xp3 = false, tjsBootstrap = false;
    for (const auto &entry : fs->list("")) {
      if (entry.type != Kotonoha::Filesystem::FileType::Regular)
        continue;
      auto extension = std::filesystem::path(entry.path).extension().string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char ch) { return std::tolower(ch); });
      scenario = scenario || extension == ".ks";
      xp3 = xp3 || extension == ".xp3";
      auto name = std::filesystem::path(entry.path).filename().string();
      std::transform(
          name.begin(), name.end(), name.begin(),
          [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      tjsBootstrap = tjsBootstrap || name == "startup.tjs";
    }
    if (!scenario && !xp3 && !tjsBootstrap) {
      info = "No Kirikiri scenario (.ks), startup.tjs or XP3 archive found";
      return false;
    }
    info = "Virtual Kirikiri source validated";
    return true;
  } catch (const std::exception &error) {
    info = error.what();
    return false;
  }
}

} // namespace

// The port owns game-specific initialization and transitions. The shared
// SDLCallbackApp.hpp defines the four SDL callback entrypoints below.
struct KotonohaPortCallbacks final {
  static SDL_AppResult init(void **state, int argc, char **argv);
  static SDL_AppResult event(void *state, SDL_Event *event);
  static SDL_AppResult iterate(void *state);
  static void quit(void *state, SDL_AppResult result);
};

SDL_AppResult KotonohaPortCallbacks::init(void **state, int argc, char **argv) {
  *state = nullptr;
  KOTONOHA_SET_APP_METADATA("Kirikiri", "me.hirameki.kotonoha.kirikiri");
  try {
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("Kirikiri", options);
    cli.option("--kirikiri", "DIRECTORY|ISO", "Kirikiri2/KirikiriZ game data",
               directory);
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
          arg == "--ass-style" || arg == "--frames" || arg == "--kirikiri") {
        filteredArgv.push_back(argv[i]);
        if (i + 1 < argc)
          filteredArgv.push_back(argv[++i]);
      } else if (!arg.empty() && arg[0] != '-' && directory.empty()) {
        directory = arg;
      } else {
        filteredArgv.push_back(argv[i]);
      }
    }
    cli.parse(static_cast<int>(filteredArgv.size()), filteredArgv.data());
    if (const char *vid = SDL_getenv("SDL_VIDEODRIVER");
        vid && std::string(vid) == "dummy") {
      options.persistSettings = false;
      options.settings.renderer = "software";
    }
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
    // Honor the shared default and explicit --no-launcher. Automated
    // --frames and MCP runs bypass the launcher in CommandLine::parse().
    if (directory.empty() && !options.launcher)
      throw std::runtime_error(
          "No valid Kirikiri source; pass --kirikiri DIRECTORY|ISO");
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

SDL_AppResult KotonohaPortCallbacks::event(void *state, SDL_Event *event) {
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

SDL_AppResult KotonohaPortCallbacks::iterate(void *state) {
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

void KotonohaPortCallbacks::quit(void *state, SDL_AppResult) {
  delete static_cast<App *>(state);
}

KOTONOHA_SDL_CALLBACK_APP(KotonohaPortCallbacks)
