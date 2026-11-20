#include <SummerRuntime.hpp>

#include <Kotonoha/core/Engine.hpp>
#include <SummerRadish/SummerAssets.hpp>

#include <SDL3/SDL.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    if (argc != 2)
      return 77;
    auto assets = std::make_shared<Kotonoha::Providers::SummerAssets>(argv[1]);
    SummerRadish::SummerRuntime game(assets, "Miss Each Other",
                                    "me.hirameki.kotonoha.meo");
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    Kotonoha::Engine engine(game, options);
    struct Stop {
      SummerRadish::SummerRuntime &game;
      ~Stop() { game.shutdown(); }
    } stop{game};

    for (int i = 0; i < 5 && game.GetState() != Kotonoha::GameState::Gameplay;
         ++i)
      engine.Iterate();
    if (game.GetState() != Kotonoha::GameState::Gameplay ||
        game.GetActiveScenes().empty())
      throw std::runtime_error("MEO has no active gameplay scene");
    if (game.name != "Miss Each Other")
      throw std::runtime_error("MEO window identity is incorrect");

    auto &renderer = engine.GetRenderer();
    auto *previous = SDL_GetRenderTarget(&renderer);
    Kotonoha::TexturePtr target(SDL_CreateTexture(
        &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 960,
        720));
    if (!target || !SDL_SetRenderTarget(&renderer, target.get()))
      throw std::runtime_error(SDL_GetError());
    bool visible = false;
    const Uint64 started = SDL_GetTicks();
    while (!visible && SDL_GetTicks() - started < 5000) {
      engine.Iterate();
      game.render(renderer, 1);
      Kotonoha::SurfacePtr surface(SDL_RenderReadPixels(&renderer, nullptr));
      if (!surface)
        throw std::runtime_error(SDL_GetError());
      const auto *pixels = static_cast<const uint8_t *>(surface->pixels);
      uint64_t light = 0;
      for (int y = 80; y < surface->h - 80; y += 16)
        for (int x = 80; x < surface->w - 80; x += 16) {
          const auto *pixel = pixels + y * surface->pitch + x * 4;
          light += pixel[0] + pixel[1] + pixel[2];
        }
      visible = light > 5000;
      SDL_Delay(20);
    }
    SDL_SetRenderTarget(&renderer, previous);
    if (!visible)
      throw std::runtime_error("MEO gameplay remained black");
    std::cout << "MEO rendered its opening scene\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
