// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Retail AIR 1:26:0 opening-movie integration. The command is discovered in
// Seen.txt instead of hardcoding a translated scenario index.
#include "RealLiveRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <RealLive/RealLiveAssets.hpp>
#include <RealLive/Script.hpp>
#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::string data = argc > 1 ? argv[1] : "";
    if (data.empty()) {
      if (const char *env = std::getenv("REALLIVE_TEST_DATA"))
        data = env;
    }
    if (data.empty() || !std::filesystem::exists(data)) {
      std::cout << "skip - REALLIVE_TEST_DATA not provided\n";
      return 0;
    }
    SDL_SetAppMetadata("RealLive Movie Test", "0.1.0",
                       "me.hirameki.kotonoha.reallive.movie.test");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
      throw std::runtime_error(SDL_GetError());
    struct SDLSession {
      ~SDLSession() { SDL_Quit(); }
    } session;

    auto assets =
        std::make_shared<Kotonoha::Providers::RealLiveAssets>(data, "");
    auto archive = assets->scripts();
    RLMachine machine;
    int movies = 0;
    std::string movieNode, movieName;
    for (int scenario : archive.ScenarioNumbers()) {
      std::size_t index = 0;
      for (const auto &element : archive.Scenario(scenario)) {
        const auto *command =
            dynamic_cast<const libreallive::CommandElement *>(element.get());
        if (command && command->modtype() == 1 && command->module() == 26 &&
            command->opcode() == 0 && command->overload() == 0) {
          ++movies;
          movieNode = std::to_string(scenario) + ":" + std::to_string(index);
          movieName = RealLive::StringParameter(machine, *command, 0);
        }
        ++index;
      }
    }
    require(movies == 1, "Retail AIR must contain exactly one 1:26:0 movie");
    require(movieName == "op_air.mpg",
            "Retail AIR movie command must reference op_air.mpg");
    require(assets->contains("MOV/" + movieName),
            "MOV/op_air.mpg resolves through the public AssetProvider API");

    RealLive::RealLiveRuntime game(assets);
    game.SetState(Kotonoha::GameState::Menu);
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    options.rendererOverride = "software";
    Kotonoha::Engine engine(game, options);
    struct Guard {
      RealLive::RealLiveRuntime &game;
      ~Guard() { game.shutdown(); }
    } guard{game};
    SDL_SetRenderVSync(&engine.GetRenderer(), 0);
    auto channel = engine.GetAudio().channel("BGM");
    require(channel != nullptr, "Movie audio channel exists");

    game.debugJumpToNode(movieNode);
    int waited = 0;
    for (; waited < 400 && channel->activeStreams() == 0; ++waited) {
      engine.Iterate();
      require(game.diagnostic().empty(), game.diagnostic().c_str());
      SDL_Delay(16);
    }
    require(channel->activeStreams() > 0,
            "Opening movie contributes its embedded audio stream");
    const std::string whilePlaying = game.debugNode();
    for (int n = 0; n < 30; ++n) {
      engine.Iterate();
      SDL_Delay(16);
    }
    require(game.debugNode() == whilePlaying,
            "Script stays blocked while the opening movie plays");

    SDL_Event skip{};
    skip.type = SDL_EVENT_KEY_DOWN;
    skip.key.key = SDLK_ESCAPE;
    skip.key.repeat = 0;
    game.onEvent(skip);
    for (int n = 0; n < 20 && game.debugNode() == whilePlaying; ++n)
      engine.Iterate();
    require(game.debugNode() != whilePlaying,
            "Explicit key resumes the script after skipping the movie");
    std::cout << "REALLIVE AIR opening movie PASS at " << movieNode << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "REALLIVE AIR opening movie FAIL: " << error.what() << '\n';
    return 1;
  }
}
