#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "PmailRuntime.hpp"

#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

#include <SnowRadish/SnowAssets.hpp>

namespace {

struct Application {
  Kotonoha::PmailPort::PmailRuntime game;
  Kotonoha::Engine engine;
  long framesRemaining = -1;
  ~Application() { game.shutdown(); }

  Application(std::string directory, Kotonoha::EngineOptions options)
      : game(directory,
             std::dynamic_pointer_cast<Kotonoha::Providers::SnowAssets>(
                 options.assets)),
        engine(game, std::move(options)) {}
};

std::string DefaultDirectory() {
  // Prefer the original disc layout if present in workspace/dev env.
  const char *candidates[] = {
      "/Volumes/Nero 256GB/KotonohaAssets/PMAILDVD",
      "/Volumes/Nero 256GB/KotonohaAssets/PMAILDVD/data",
      nullptr,
  };
  for (int i = 0; candidates[i]; ++i) {
    std::error_code ec;
    if (std::filesystem::exists(candidates[i], ec))
      return candidates[i];
  }
  return Kotonoha::gameAssetDirectory("pmail").string();
}

} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  *appstate = nullptr;
  SDL_SetAppMetadata("Pure Mail", "0.1.0", "me.hirameki.kotonoha.pmail");
  try {
    Kotonoha::EngineOptions options;
    options.persistSettings = true;
    std::string directory;
    Kotonoha::CommandLine arguments("Pmail", options);
    arguments.option("--pmail", "DIRECTORY",
                     "Pure Mail game directory (PMAILDVD root or data folder)",
                     directory);
    arguments.parse(argc, argv);
    if (arguments.wantsHelp()) {
      std::fputs(arguments.help().c_str(), stdout);
      return SDL_APP_SUCCESS;
    }
    if (options.settingsPath.empty() && options.persistSettings)
      options.settingsPath = Kotonoha::EngineSettings::defaultPath();
    if (directory.empty()) {
      const auto env = SDL_getenv("PMAIL_DIR");
      if (env)
        directory = env;
    }
    if (directory.empty())
      directory = DefaultDirectory();

    options.assets =
        std::make_shared<Kotonoha::Providers::SnowAssets>(directory);
    auto *app = new Application(directory, std::move(options));
    app->framesRemaining = arguments.frames;
    *appstate = app;
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialization failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  auto *app = static_cast<Application *>(appstate);
  try {
    return app->engine.Event(event);
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  auto *app = static_cast<Application *>(appstate);
  try {
    const auto result = app->engine.Iterate();
    if (result != SDL_APP_CONTINUE)
      return result;
    if (app->framesRemaining > 0 && --app->framesRemaining == 0)
      return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
  } catch (const std::exception &error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s",
                 error.what());
    return SDL_APP_FAILURE;
  }
}

void SDL_AppQuit(void *appstate, SDL_AppResult) {
  delete static_cast<Application *>(appstate);
}
