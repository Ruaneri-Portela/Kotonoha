#include <Kotonoha/core/SDLCallbackApp.hpp>
#include <SDL3/SDL.h>

#include "ZeroLauncher.hpp"
#include "ZeroRuntime.hpp"
#include <ZeroNoTsukaima/DiscAssetProvider.hpp>

#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <cstdio>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// The disc is the engine AssetProvider; the runtime keeps the typed handle.
Kotonoha::EngineOptions
WithAssets(Kotonoha::EngineOptions options,
           std::shared_ptr<Kotonoha::AssetProvider> assets) {
  options.assets = std::move(assets);
  return options;
}

struct Application {
  ZeroNoTsukaima::ZeroRuntime game;
  Kotonoha::Engine engine;
  long framesLeft = -1; // --frames: smoke runs quit by themselves

  Application(std::shared_ptr<Kotonoha::Providers::ZeroDiscAssets> disc,
              Kotonoha::EngineOptions options)
      : game(disc), engine(game, WithAssets(std::move(options), disc)) {}
  // The engine (renderer, mixer) goes before the runtime: free the session
  // first.
  ~Application() { game.Shutdown(); }
};

// With --launcher the launcher picks the disc and the engine settings first.
struct App {
  std::unique_ptr<ZeroNoTsukaima::ZeroLauncher> launcher;
  std::unique_ptr<Application> game;
  Kotonoha::EngineOptions options;
  long frames = -1;

  void Start(const std::string &disc) {
    auto provider = std::make_shared<Kotonoha::Providers::ZeroDiscAssets>(disc);
    provider->setOverlay(options.assetOverlayDirectory());
    game = std::make_unique<Application>(std::move(provider), options);
    game->framesLeft = frames;
  }
};

std::string DefaultDisc() {
  for (const char *disc :
       {"ZeroNoTsukaima.bin", "ZeroNoTsukaima.iso", "ZeroNoTsukaima1.bin",
        "ZeroNoTsukaima1.iso", "ZeroNoTsukaima0.bin", "ZeroNoTsukaima0.iso",
        "ZeroNoTsukaima2.bin", "ZeroNoTsukaima2.iso"})
    if (auto found =
            Kotonoha::FileResolver::findGameAsset("zero-no-tsukaima", disc))
      return found->string();
  return (Kotonoha::gameAssetDirectory("zero-no-tsukaima") /
          "ZeroNoTsukaima.bin")
      .string();
}

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
  KOTONOHA_SET_APP_METADATA("Zero no Tsukaima",
                            "me.hirameki.kotonoha.zeronotsukaima");
  try {
    auto app = std::make_unique<App>();
    Kotonoha::EngineOptions &options = app->options;
    options.persistSettings = true;
    std::string disc;

    Kotonoha::CommandLine arguments("ZeroNoTsukaima", options);
    arguments.option("--zero", "DISC.bin", "Zero no Tsukaima game disc", disc);
    arguments.parse(argc, argv);
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    app->frames = arguments.frames;
    if (options.enableMCP && options.settingsPath.empty())
      options.persistSettings = false;
    if (options.settingsPath.empty() && options.persistSettings)
      options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    const std::string settingsPath =
        options.persistSettings ? options.settingsPath : std::string();
    const auto env = SDL_getenv("ZERO_DISC");
    disc = Kotonoha::GameLauncher::findGamePath(
        "zero-no-tsukaima",
        {disc, env ? env : "",
         Kotonoha::GameLauncher::rememberedPath(settingsPath, "disc.txt"),
         DefaultDisc()},
        ZeroNoTsukaima::ZeroLauncher::validatePath,
        {"ZeroNoTsukaima.bin", "ZeroNoTsukaima.iso", "ZeroNoTsukaima1.bin",
         "ZeroNoTsukaima1.iso", "ZeroNoTsukaima0.bin", "ZeroNoTsukaima0.iso",
         "ZeroNoTsukaima2.bin", "ZeroNoTsukaima2.iso"});
    // Honor the shared default and explicit --no-launcher. Automated
    // --frames and MCP runs bypass the launcher in CommandLine::parse().
    if (disc.empty() && !options.launcher)
      throw std::runtime_error(
          "No valid Zero no Tsukaima source; pass --zero DISC.bin");

    if (options.launcher) {
      app->launcher =
          std::make_unique<ZeroNoTsukaima::ZeroLauncher>(settingsPath, disc);
      app->launcher->open();
    } else {
      app->Start(disc);
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
  auto *app = static_cast<App *>(appstate);
  try {
    if (app->launcher)
      return app->launcher->event(*event);
    return app->game->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult KotonohaPortCallbacks::iterate(void *appstate) {
  auto *app = static_cast<App *>(appstate);
  try {
    if (app->launcher) {
      app->launcher->iterate();
      switch (app->launcher->state()) {
      case ZeroNoTsukaima::ZeroLauncher::State::Running:
        return SDL_APP_CONTINUE;
      case ZeroNoTsukaima::ZeroLauncher::State::Quit:
        return SDL_APP_SUCCESS;
      case ZeroNoTsukaima::ZeroLauncher::State::Launch:
        break;
      }
      // The launcher window and ImGui context go before the engine starts.
      const std::string disc = app->launcher->path();
      app->options.settings = app->launcher->settings();
      app->launcher.reset();
      app->Start(disc);
      return SDL_APP_CONTINUE;
    }
    Application &game = *app->game;
    const SDL_AppResult result = game.engine.Iterate();
    if (game.framesLeft > 0 && --game.framesLeft == 0)
      return SDL_APP_SUCCESS;
    return result;
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
