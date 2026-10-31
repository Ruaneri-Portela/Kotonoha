#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "DdlcAssets.hpp"
#include "DdlcLauncher.hpp"
#include "DdlcRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <cstdio>
#include <Kotonoha/core/FileSystem.hpp>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

Kotonoha::EngineOptions withAssets(Kotonoha::EngineOptions options,
                                  const std::shared_ptr<Ddlc::GameAssets>& assets) {
    options.assets = assets;
    return options;
}

struct Application {
    Ddlc::GameRuntime game;
    Kotonoha::Engine engine;
    long framesLeft = -1;

    Application(const std::shared_ptr<Ddlc::GameAssets>& assets, Kotonoha::EngineOptions options)
        : game(assets), engine(game, withAssets(std::move(options), assets)) {}
    ~Application() { game.shutdown(); }
};

struct App {
    std::unique_ptr<Ddlc::GameLauncher> launcher;
    std::unique_ptr<Application> game;
    Kotonoha::EngineOptions options;
    long frames = -1;

    void start(const std::string& directory) {
        auto assets = std::make_shared<Ddlc::GameAssets>(directory);
        game = std::make_unique<Application>(assets, options);
        game->framesLeft = frames;
    }
};

// Game data: <root>/game-assets/game (bundles, OBB, working or executable
// directory, documents), then <root>/game and the source checkout layouts.
std::string defaultDirectory() {
    constexpr const char* game = "doki-doki-literature-club";
    if (auto found = Kotonoha::FileResolver::findGameAsset(game, "game/scripts.rpa"))
        return found->parent_path().string();
    for (const char* relative : {"game/scripts.rpa", "doki-doki-literature-club/game/scripts.rpa",
                                 "games/doki-doki-literature-club/game-assets/game/scripts.rpa"})
        if (auto found = Kotonoha::FileResolver::find(relative, game))
            return found->parent_path().string();
    return (Kotonoha::gameAssetDirectory(game) / "game").string();
}


} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    *appstate = nullptr;
    SDL_SetAppMetadata("Doki Doki Literature Club!", "0.1.0", "me.hirameki.kotonoha.dokidokiliteratureclub");
    try {
        auto app = std::make_unique<App>();
        auto& options = app->options;
        options.persistSettings = true;
        std::string directory, dumpPath;
        bool launcher = false;
        Kotonoha::CommandLine arguments("DokiDoki", options);
        arguments.option("--ddlc", "DIRECTORY", "DDLC directory containing scripts.rpa", directory);
        arguments.option("--dump-translation", "FILE", "Export original texts by route and exit", dumpPath);
        arguments.flag("--launcher", "Open the DDLC selection launcher", launcher);
        arguments.parse(argc, argv);
        if (arguments.wantsHelp()) { std::fputs(arguments.help().c_str(), stdout); return SDL_APP_SUCCESS; }
        app->frames = arguments.frames;
        if (launcher && !dumpPath.empty())
            throw std::invalid_argument("--dump-translation cannot be combined with --launcher");
        if (options.enableMCP && options.settingsPath.empty()) options.persistSettings = false;
        if (options.settingsPath.empty() && options.persistSettings)
            options.settingsPath = Kotonoha::EngineSettings::defaultPath("doki-doki-literature-club");
        const std::string settingsPath = options.persistSettings ? options.settingsPath : std::string();
        if (directory.empty())
            if (const char* value = std::getenv("DDLC_DIR"); value && *value) directory = value;
        if (directory.empty()) directory = defaultDirectory();
        if (directory.empty()) directory = Kotonoha::GameLauncher::rememberedPath(settingsPath, "dir.txt");
        if (launcher) {
            app->launcher = std::make_unique<Ddlc::GameLauncher>(settingsPath, directory);
            app->launcher->open();
        } else {
            app->start(directory);
        }
        if (!dumpPath.empty()) {
            app->game->engine.dumpTranslation(dumpPath);
            SDL_Log("Translation dictionary written to %s", dumpPath.c_str());
            return SDL_APP_SUCCESS;
        }
        *appstate = app.release();
        return SDL_APP_CONTINUE;
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialization failed: %s", error.what());
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<App*>(appstate);
    try {
        if (app->launcher) return app->launcher->event(*event);
        return app->game->engine.Event(event);
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event failed: %s", error.what());
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<App*>(appstate);
    try {
        if (app->launcher) {
            app->launcher->iterate();
            if (app->launcher->state() == Ddlc::GameLauncher::State::Running) return SDL_APP_CONTINUE;
            if (app->launcher->state() == Ddlc::GameLauncher::State::Quit) return SDL_APP_SUCCESS;
            const std::string directory = app->launcher->path();
            app->options.settings = app->launcher->settings();
            app->launcher.reset();
            app->start(directory);
            return SDL_APP_CONTINUE;
        }
        const SDL_AppResult result = app->game->engine.Iterate();
        if (app->game->framesLeft > 0 && --app->game->framesLeft == 0) return SDL_APP_SUCCESS;
        return result;
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s", error.what());
        return SDL_APP_FAILURE;
    }
}

void SDL_AppQuit(void* appstate, SDL_AppResult) { delete static_cast<App*>(appstate); }
