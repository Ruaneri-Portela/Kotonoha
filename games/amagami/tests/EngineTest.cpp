#include "AmagamiRuntime.hpp"
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
void require(bool b, const std::string &s) {
  if (!b)
    throw std::runtime_error(s);
}
void settle(Amagami::AmagamiRuntime &game) {
  for (size_t i = 0; game.session() && game.session()->waiting() && i < 100;
       ++i)
    game.updatePresentation(30);
  require(!game.session() || !game.session()->waiting(),
          "Presentation reaches input");
}
void press(Kotonoha::Engine &engine, SDL_Keycode key) {
  SDL_Event e{};
  e.type = SDL_EVENT_KEY_DOWN;
  e.key.key = key;
  e.key.down = true;
  e.key.windowID = SDL_GetWindowID(&engine.GetWindow());
  engine.Event(&e);
  settle(dynamic_cast<Amagami::AmagamiRuntime &>(engine.GetRuntime()));
}
void capture(Kotonoha::Engine &e, Amagami::AmagamiRuntime &game,
             const std::string &name) {
  const char *dir = SDL_getenv("AMAGAMI_CAPTURE_DIR");

  auto *r = &e.GetRenderer();
  auto *previous = SDL_GetRenderTarget(r);
  auto *texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_TARGET, 960, 672);
  require(texture != nullptr, SDL_GetError());
  SDL_SetRenderTarget(r, texture);
  game.render(*r, 1);
  game.renderOverlay(*r, 1);
  auto *pixels = SDL_RenderReadPixels(r, nullptr);
  require(pixels != nullptr, SDL_GetError());
  if (name.rfind("prologue", 0) == 0) {
    Uint8 red, green, blue, alpha;
    require(SDL_ReadSurfacePixel(pixels, 480, 100, &red, &green, &blue, &alpha),
            SDL_GetError());
    require(red > 80 && green > 40,
            "Opening camera renders the sky without black borders");
  }
  if (dir) {
    auto path = (std::filesystem::path(dir) / (name + ".png")).string();
    require(SDL_SavePNG(pixels, path.c_str()), SDL_GetError());
  }
  SDL_DestroySurface(pixels);
  SDL_SetRenderTarget(r, previous);
  SDL_DestroyTexture(texture);
}
int main(int argc, char **argv) {
  try {
    require(argc == 2 || argc == 3,
            "Expected disc and optional translation dictionary");
    auto source = std::make_shared<Amagami::Source>(argv[1]);
    auto assets = std::make_shared<Amagami::Assets>(source);
    // Overlapping transparent mesh fragments must preserve the face underneath.
    auto body = assets->loadSurface("actor-body/stand/b7_Aax.tm2");
    const auto bounds = assets->bodyBounds("stand/b7_Aax.tm2");
    for (const SDL_Point p : {SDL_Point{400, 395}, SDL_Point{400, 422}}) {
      Uint8 r, g, b, a;
      require(SDL_ReadSurfacePixel(body.get(), p.x - bounds.x, p.y - bounds.y,
                                   &r, &g, &b, &a),
              SDL_GetError());
      require(a > 240 && r > 100, "Transparent overlapping fragments must not "
                                  "cut holes in Miya's face");
    }
    require(assets->contains("epi/e6_24a0.tm2"),
            "Retail event picture resolves");
    require(assets->loadSurface("epi/e6_24a0.tm2") != nullptr,
            "Retail event picture decodes");
    Amagami::AmagamiRuntime game(assets);
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    if (argc == 3)
      options.translationPath = argv[2];
    Kotonoha::Engine engine(game, options);
    struct Shutdown {
      Amagami::AmagamiRuntime &game;
      ~Shutdown() { game.shutdown(); }
    } shutdown{game};
    game.SetState(Kotonoha::GameState::Menu);
    engine.Iterate();
    capture(engine, game, "title");
    press(engine, SDLK_RETURN);
    require(!game.session(),
            "Press Start opens original main menu before starting");
    capture(engine, game, "main-menu");
    game.debugJumpToNode("n0dea_ev01A");
    require(game.session()->waiting() && game.session()->history().empty(),
            "Opening scene obeys script delay");
    settle(game);
    engine.Iterate();
    require(game.session() && !game.session()->dialogue().text.empty(),
            "First original dialogue");
    auto first = game.session()->history().size();
    bool named = false;
    Kotonoha::TextInputDialog nameDialog(engine, "", nullptr, nullptr);
    nameDialog.configure(
        "Save name",
        [&](const std::string &) {
          named = true;
          game.RemoveScene(&nameDialog);
        },
        nullptr);
    game.AddScene(&nameDialog);
    SDL_Event confirmName{};
    confirmName.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    confirmName.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    engine.Event(&confirmName);
    require(named && game.session()->history().size() == first,
            "Controller confirmation reaches a shared native dialog without "
            "advancing Amagami");
    press(engine, SDLK_RETURN);
    require(game.session()->history().size() == first,
            "First confirm reveals text without advancing");
    capture(engine, game, "prologue");
    auto beforePan = game.session()->visuals().at("BG").y;
    game.updatePresentation(2);
    require(game.session()->visuals().at("BG").y < beforePan,
            "Camera pans while text waits for player input");
    capture(engine, game, "prologue-pan");
    const auto nativeUpscaling = engine.GetUpscaler().getSettings();
    engine.GetUpscaler().configure({Kotonoha::UpscaleBackend::Linear,
                                    Kotonoha::UpscalePreset::Anime4K, 2});
    capture(engine, game, "prologue-upscaled");
    engine.GetUpscaler().configure(nativeUpscaling);
    Amagami::AmagamiTextBox *box = nullptr;
    for (auto *scene : game.GetActiveScenes())
      if (auto *candidate = dynamic_cast<Amagami::AmagamiTextBox *>(scene))
        box = candidate;
    require(box != nullptr,
            "Amagami dialogue is registered as an engine component");
    const auto key = game.session()->dialogue().original;
    const auto route = std::to_string(game.translationRoute());
    const auto sources = game.translationSources();
    require(sources.size() == 2309 && sources.at(1227).size() == 411 &&
                !sources.at(-1).empty(),
            "Native translation export includes every scenario and original "
            "prologue key");
    if (argc == 3) {
      require(
          box->getText() == engine.GetTranslation().text(1227, key) &&
              box->getText() != Amagami::DisplayText(key),
          "Portuguese dictionary is active in the actual dialogue component");
    }
    engine.setAgentPaused(true);
    const auto beforeNativeStep = *game.debugNodeSeconds();
    engine.advanceTimeline(0.5);
    require(*game.debugNodeSeconds() == beforeNativeStep + 0.5,
            "MCP steps the native clock without a generic VN timeline");
    engine.setAgentPaused(false);
    const std::string translated = "Live Translation: Fewer bugs, More story!";
    engine.setTranslation(route, key, translated);
    require(box->getText() == translated && box->getVisibleText() == translated,
            "Live translation immediately updates current dialogue without "
            "eating ASCII words");
    require(game.session()->history().size() == first,
            "Live editing does not advance the VM");
    capture(engine, game, "live-translation");
    engine.setTranslation(route, key, "");
    require(box->getText() == Amagami::DisplayText(key),
            "Removing live translation restores original readable text");
    for (unsigned n = 0; n < 200 && game.session()->choices().empty(); ++n)
      press(engine, SDLK_RETURN);
    require(game.session()->choices().size() == 3, "Runtime reaches choice");
    capture(engine, game, "choice");
    auto choiceHistory = game.session()->history().size();
    press(engine, SDLK_DOWN);
    require(game.session()->choices().size() == 3 &&
                game.session()->history().size() == choiceHistory,
            "Choice navigation does not advance story");
    press(engine, SDLK_RETURN);
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
    press(engine, SDLK_RETURN);
    engine.Iterate();
    // Exercise the actual archive surface decoder and fragmented character
    // mesh.
    game.render(engine.GetRenderer(), 1);
    capture(engine, game, "miya");
    auto &upscaler = engine.GetUpscaler();
    const auto originalUpscaling = upscaler.getSettings();
    upscaler.configure({Kotonoha::UpscaleBackend::Linear,
                        Kotonoha::UpscalePreset::Anime4K, 2});
    auto &originalTexture =
        assets->texture(engine.GetRenderer(), actor->second.file);
    auto &upscaledTexture = upscaler.process(originalTexture, 0);
    require(upscaledTexture.w == originalTexture.w * 2 &&
                upscaledTexture.h == originalTexture.h * 2,
            "Amagami textures use configured engine upscaling");
    capture(engine, game, "miya-upscaled");
    upscaler.configure(originalUpscaling);
    press(engine, SDLK_ESCAPE);
    capture(engine, game, "pause");
    press(engine, SDLK_ESCAPE);
    for (size_t i = 0; i < 500; ++i) {
      auto miya = game.session()->visuals().find("MIY");
      if (miya != game.session()->visuals().end() &&
          miya->second.breathAge >= 0)
        break;
      press(engine, SDLK_RETURN);
    }
    auto coldMiya = game.session()->visuals().find("MIY");
    require(coldMiya != game.session()->visuals().end() &&
                coldMiya->second.breathAge >= 0,
            "Original outdoor scene enables cold breathing");
    box->completeDialogue();
    game.updatePresentation(0.2);
    capture(engine, game, "miya-breath");
    require(assets->contains("sysgraph/breath1.tm2") &&
                assets->contains("sysgraph/breath3.tm2"),
            "Breath uses original disc particle textures");
    require(assets->cachedTextures() > 0, "Disc textures rendered by engine");
    std::cout << "Amagami engine: dialogue, choice, actor, pause passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
