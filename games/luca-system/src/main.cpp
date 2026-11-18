#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/GameSourceProbe.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/DiscImage.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/GameLauncher.hpp>
#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <LucaSystem/LucaSystemAssets.hpp>
#include <LucaSystem/LucaSystemRuntime.hpp>
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
           std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> assets) {
  options.assets = std::move(assets);
  return options;
}

struct App {
  Kotonoha::EngineOptions options;
  long frames = -1;
  std::unique_ptr<Kotonoha::FolderGameLauncher> launcher;
  struct Game {
    LucaSystem::LucaSystemRuntime runtime;
    Kotonoha::Engine engine;
    ~Game() { runtime.shutdown(); }
    Game(std::shared_ptr<Kotonoha::Providers::LucaSystemAssets> a,
         Kotonoha::EngineOptions o)
        : runtime(a), engine(runtime, withAssets(std::move(o), std::move(a))) {}
  };
  std::unique_ptr<Game> game;

  void start(const std::string &directory) {
    auto assets = std::shared_ptr<Kotonoha::Providers::LucaSystemAssets>(
        new Kotonoha::Providers::LucaSystemAssets(
            directory, options.assetOverlayDirectory()));
    game = std::make_unique<Game>(std::move(assets), options);
  }
};

bool validate(const std::string &path, std::string &info) {
  try {
    // The runtime reads PAKs through the virtual filesystem. Validate the
    // same view, including ISO images and case-insensitive archive filenames,
    // instead of accepting any well-formed but unrelated ISO.
    auto source = Kotonoha::Filesystem::Filesystem::mount(path);
    if (!Kotonoha::Filesystem::findRequiredFilesPrefix(
            *source, {"PARAM.PAK", "SCRIPT.PAK", "SYSSE.PAK"},
            {"", "files/"})) {
      info = "LucaSystem requires PARAM.PAK, SCRIPT.PAK and SYSSE.PAK";
      return false;
    }
    info = "LucaSystem archives present in virtual game source";
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
  KOTONOHA_SET_APP_METADATA("LucaSystem", "me.hirameki.kotonoha.lucasystem");
  try {
    Kotonoha::migrateLegacyDocuments("me.hirameki.kotonoha.luca_system");
    auto app = std::make_unique<App>();
    auto &options = app->options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine cli("LucaSystem", options);
    cli.option("--luca-system", "DIRECTORY|ISO", "LucaSystem game data",
               directory);
    cli.parse(argc, argv);
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
    const auto env = SDL_getenv("LUCA_SYSTEM_DIR");
    const auto downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
    directory = Kotonoha::GameLauncher::findGamePath(
        "luca-system",
        {directory, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt"),
         Kotonoha::gameAssetDirectory("luca-system").string(),
         downloads ? (std::filesystem::path(downloads) / "LucaSystem").string()
                   : ""},
        validate, {""}, true);
    // Honor the shared default and explicit --no-launcher. Automated
    // --frames and MCP runs bypass the launcher in CommandLine::parse().
    if (directory.empty() && !options.launcher)
      throw std::runtime_error(
          "No valid LucaSystem source; pass --luca-system DIRECTORY|ISO");
    if (options.launcher) {
      app->launcher = std::make_unique<Kotonoha::FolderGameLauncher>(
          "LucaSystem", settingsPath, directory, validate, options.settings,
          true);
      app->launcher->open();
    } else {
      app->start(directory);
    }
    *state = app.release();
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LucaSystem: %s", error.what());
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
