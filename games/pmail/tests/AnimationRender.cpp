#include "PmailStage.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Kotonoha::PmailPort;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Runtime : Kotonoha::Runtime {
  Runtime()
      : Kotonoha::Runtime("Animation test", "1",
                          "me.hirameki.kotonoha.pmail.animation-test") {}
  SDL_Point viewport() const override { return {800, 600}; }
  void initSplash(Kotonoha::Engine &) override {}
};
void checkPreset(PmailGame &game, Kotonoha::Providers::SnowAssets &assets,
                 Kotonoha::Engine &engine, uint32_t preset) {
  auto &renderer = engine.GetRenderer();
  PmailStage stage(engine, game, assets);
  game.native(game.vm(), 69, {{0}, {0}, {0}});
  game.native(game.vm(), 51, {{preset}, {245}, {152}});
  const auto &definition = AnimationPresets[preset];
  auto *target = SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_TARGET, 800, 600);
  require(target && SDL_SetRenderTarget(&renderer, target),
          "Cannot create test render target");
  auto expected =
      Kotonoha::SurfacePtr(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32));
  require(bool(expected), "Cannot create reference canvas");
  const auto clear = [&] {
    SDL_FillSurfaceRect(expected.get(), nullptr,
                        SDL_MapSurfaceRGBA(expected.get(), 0, 0, 0, 255));
  };
  clear();
  const auto count = game.animations().back().count;
  for (uint32_t frame = 0; frame < count; ++frame) {
    if (definition.restore)
      clear();
    auto source =
        assets.loadSurfaceAsset("graphic/" + std::string(definition.name) +
                                ".anim#" + std::to_string(frame));
    const auto &placement =
        assets.AnimationOf(definition.name).frames.at(frame);
    SDL_Rect dest{245 + int(placement.x), 152 + int(placement.y), source->w,
                  source->h};
    SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_BLEND);
    require(SDL_BlitSurface(source.get(), nullptr, expected.get(), &dest),
            "Reference composition failed");
    stage.render(renderer, 1);
    auto captured =
        Kotonoha::SurfacePtr(SDL_RenderReadPixels(&renderer, nullptr));
    require(bool(captured), "Cannot capture stage");
    auto rgba = Kotonoha::SurfacePtr(
        SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32));
    require(rgba && rgba->w == 800 && rgba->h == 600,
            "Incorrect stage dimensions");
    for (int y = 0; y < 600; ++y) {
      const auto *actual =
          static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch;
      const auto *reference =
          static_cast<const uint8_t *>(expected->pixels) + y * expected->pitch;
      for (int x = 0; x < 800; ++x)
        for (int c = 0; c < 3; ++c)
          if (std::abs(int(actual[x * 4 + c]) - int(reference[x * 4 + c])) > 1)
            throw std::runtime_error(std::string(definition.name) + " frame " +
                                     std::to_string(frame) + " differs at " +
                                     std::to_string(x) + "," +
                                     std::to_string(y));
    }
    if (preset == 0 && (frame == 0 || frame == 40 || frame == 79)) {
      const auto path = std::filesystem::temp_directory_path() /
                        ("pmail-osboot-" + std::to_string(frame) + ".bmp");
      SDL_SaveBMP(rgba.get(), path.string().c_str());
    }
    game.advanceAnimation(definition.frameMs);
  }
  SDL_SetRenderTarget(&renderer, nullptr);
  SDL_DestroyTexture(target);
}
void checkMouth(PmailGame &game, Kotonoha::Providers::SnowAssets &assets,
                Kotonoha::Engine &engine) {
  auto object = std::make_shared<Pmail::ScriptObject>();
  object->id = 0xffe;
  object->strings = {"styu04s4"};
  game.vm().registerObject(object);
  const Pmail::Arg name{(0xffeu << 20) | (2u << 16)};
  game.native(game.vm(), 69, {{0}, {0}, {0}});
  game.native(game.vm(), 49, {{20}, {10}, name, {1}, {256}, {256}, {256}});
  game.native(game.vm(), 128, {{6}});
  game.voicePlaying = [] { return true; };
  int mouth = 0;
  game.voiceMouthFrame = [&] { return mouth; };
  auto &renderer = engine.GetRenderer();
  PmailStage stage(engine, game, assets);
  auto *target = SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_TARGET, 800, 600);
  require(target && SDL_SetRenderTarget(&renderer, target),
          "Cannot create mouth render target");
  const auto &animation = assets.AnimationOf("styu04s4");
  require(animation.regions.size() == 1 && animation.regions[0].actor == 6,
          "Unexpected mouth region");
  auto source = assets.loadSurfaceAsset("graphic/styu04s4.char");
  const auto character = assets.Sprite("styu04s4");
  auto expected =
      Kotonoha::SurfacePtr(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32));
  for (mouth = 0; mouth < 3; ++mouth) {
    SDL_FillSurfaceRect(expected.get(), nullptr,
                        SDL_MapSurfaceRGBA(expected.get(), 0, 0, 0, 255));
    SDL_Rect dest{20 + int(character.x), 10 + int(character.y), source->w,
                  source->h};
    SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(source.get(), nullptr, expected.get(), &dest);
    const auto frame = animation.regions[0].first + size_t(mouth);
    auto lips = assets.loadSurfaceAsset("graphic/styu04s4.anim#" +
                                        std::to_string(frame));
    const auto &placement = animation.frames[frame];
    dest = {20 + int(placement.x), 10 + int(placement.y), lips->w, lips->h};
    SDL_SetSurfaceBlendMode(lips.get(), SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(lips.get(), nullptr, expected.get(), &dest);
    stage.render(renderer, 1);
    auto captured =
        Kotonoha::SurfacePtr(SDL_RenderReadPixels(&renderer, nullptr));
    require(bool(captured), "Cannot capture mouth rendering");
    auto rgba = Kotonoha::SurfacePtr(
        SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32));
    for (int y = 0; y < 600; ++y) {
      const auto *actual =
          static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch;
      const auto *reference =
          static_cast<const uint8_t *>(expected->pixels) + y * expected->pitch;
      for (int x = 0; x < 800; ++x)
        for (int c = 0; c < 3; ++c)
          if (std::abs(int(actual[x * 4 + c]) - int(reference[x * 4 + c])) > 1)
            throw std::runtime_error(
                "Character or lip region rendered at the wrong position");
    }
  }
  game.voicePlaying = {};
  game.voiceMouthFrame = {};
  SDL_SetRenderTarget(&renderer, nullptr);
  SDL_DestroyTexture(target);
}

} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Usage: PmailAnimationRenderCheck PMAILDVD");
    auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(argv[1]);
    const auto path = std::filesystem::path(argv[1]) / "data/pack/script.dat";
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    PmailGame game(std::move(bytes), path.string());
    game.animationFrameCount = [&](const std::string &name) {
      return assets->AnimationOf(name).frames.size();
    };
    require(game.init(), "Game init failed");
    Runtime runtime;
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.launcher = false;
    options.persistSettings = false;
    Kotonoha::Engine engine(runtime, options);
    checkPreset(game, *assets, engine, 0);
    checkPreset(game, *assets, engine, 5);
    checkPreset(game, *assets, engine, 13);
    checkMouth(game, *assets, engine);
    std::cout << "OSBOOT, mouse, password and three actor-matched mouth frames "
                 "match CPU reference composition.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
