#include "AmagamiRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
void require(bool b, const std::string &s) {
  if (!b)
    throw std::runtime_error(s);
}
void press(Kotonoha::Engine &engine, SDL_Keycode key) {
  SDL_Event e{};
  e.type = SDL_EVENT_KEY_DOWN;
  e.key.key = key;
  e.key.down = true;
  e.key.windowID = SDL_GetWindowID(&engine.GetWindow());
  engine.Event(&e);
}
void capture(Kotonoha::Engine &e, Amagami::AmagamiRuntime &game,
             const std::string &name) {
  const char *dir = SDL_getenv("AMAGAMI_CAPTURE_DIR");
  if (!dir)
    return;
  auto *r = &e.GetRenderer();
  auto *previous = SDL_GetRenderTarget(r);
  auto *texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_TARGET, 960, 672);
  require(texture != nullptr, SDL_GetError());
  SDL_SetRenderTarget(r, texture);
  game.render(*r, 1);
  auto *pixels = SDL_RenderReadPixels(r, nullptr);
  require(pixels != nullptr, SDL_GetError());
  auto path = (std::filesystem::path(dir) / (name + ".png")).string();
  require(SDL_SavePNG(pixels, path.c_str()), SDL_GetError());
  SDL_DestroySurface(pixels);
  SDL_SetRenderTarget(r, previous);
  SDL_DestroyTexture(texture);
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "Expected disc");
    auto source = std::make_shared<Amagami::Source>(argv[1]);
    auto assets = std::make_shared<Amagami::Assets>(source);
    Amagami::AmagamiRuntime game(assets);
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    Kotonoha::Engine engine(game, options);
    struct Shutdown {
      Amagami::AmagamiRuntime &game;
      ~Shutdown() { game.shutdown(); }
    } shutdown{game};
    game.SetState(Kotonoha::GameState::Menu);
    engine.Iterate();
    capture(engine, game, "title");
    game.debugJumpToNode("n0dea_ev01A");
    engine.Iterate();
    require(game.session() && !game.session()->dialogue().text.empty(),
            "First original dialogue");
    capture(engine, game, "prologue");
    auto first = game.session()->history().size();
    press(engine, SDLK_RETURN);
    require(game.session()->history().size() == first,
            "First confirm reveals text without advancing");
    for (unsigned n = 0; n < 200 && game.session()->choices().empty(); ++n)
      press(engine, SDLK_RETURN);
    require(game.session()->choices().size() == 3, "Runtime reaches choice");
    capture(engine, game, "choice");
    press(engine, SDLK_2);
    require(game.session()->choices().empty(),
            "Keyboard selects original branch");
    for (unsigned n = 0;
         n < 320 && game.session()->dialogue().voiceCode != "MIY"; ++n)
      press(engine, SDLK_RETURN);
    require(game.error().empty(), game.error());
    require(game.session()->dialogue().voiceCode == "MIY",
            "Reached voiced Miya scene");
    auto voice = assets->loadAudio("voice/MIY/1858");
    require(voice.spec.freq == 24000 && voice.spec.channels == 1 &&
                voice.data.size() == 63459 * sizeof(float),
            "AHX exact container sample count");
    double power = 0, peak = 0;
    for (size_t offset = 0; offset < voice.data.size();
         offset += sizeof(float)) {
      float sample;
      std::memcpy(&sample, voice.data.data() + offset, sizeof(sample));
      require(std::isfinite(sample), "Finite voice sample");
      peak = std::max(peak, double(std::abs(sample)));
      power += double(sample) * sample;
    }
    auto rms = std::sqrt(power / 63459);
    require(peak < 1.03 && rms > 0.28 && rms < 0.29,
            "AHX decrypts to reference signal without crackling");
    auto music = assets->loadAudio("music/1");
    require(!music.data.empty() && music.spec.channels == 2,
            "Encrypted ADX music decoded");
    std::cout << "AHX peak=" << peak << " RMS=" << rms << '\n';
    for (unsigned n = 0; n < 160; ++n) {
      auto actor = game.session()->visuals().find("MIY");
      if (actor != game.session()->visuals().end() && actor->second.visible &&
          !actor->second.file.empty())
        break;
      press(engine, SDLK_RETURN);
    }
    auto actor = game.session()->visuals().find("MIY");
    require(actor != game.session()->visuals().end() && actor->second.visible &&
                assets->contains(actor->second.file),
            "Original Miya atlas is visible");
    // Exercise the actual archive surface decoder and fragmented character
    // mesh.
    game.render(engine.GetRenderer(), 1);
    capture(engine, game, "miya");
    press(engine, SDLK_ESCAPE);
    capture(engine, game, "pause");
    press(engine, SDLK_ESCAPE);
    require(assets->cachedTextures() > 0, "Disc textures rendered by engine");
    std::cout << "Amagami engine: dialogue, choice, actor, pause passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
