#include "SummerRuntime.hpp"
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <SummerRadish/Script.hpp>
#include <iostream>
#include <stdexcept>
using namespace Kotonoha;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class T> T *find(Runtime &r) {
  for (auto *s : r.GetActiveScenes())
    if (auto *t = dynamic_cast<T *>(s))
      return t;
  return nullptr;
}
void press(Engine &engine, SDL_Keycode key) {
  SDL_Event e{};
  e.type = SDL_EVENT_KEY_DOWN;
  e.key.windowID = SDL_GetWindowID(&engine.GetWindow());
  e.key.down = true;
  e.key.key = key;
  e.common.timestamp = SDL_GetTicksNS();
  engine.Event(&e);
  engine.Iterate();
}
void click(Engine &engine) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.down = true;
  e.button.x = 120;
  e.button.y = 120;
  e.button.windowID = SDL_GetWindowID(&engine.GetWindow());
  engine.Event(&e);
  engine.Iterate();
}
void capture(Engine &engine, Runtime &game, const char *name) {
  const char *dir = SDL_getenv("SUMMER_CAPTURE_DIR");
  if (!dir)
    return;
  std::filesystem::create_directories(dir);
  auto &r = engine.GetRenderer();
  TexturePtr target(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 960, 720));
  auto *previous = SDL_GetRenderTarget(&r);
  SDL_SetRenderTarget(&r, target.get());
  game.render(r, 1);
  SurfacePtr surface(SDL_RenderReadPixels(&r, nullptr));
  if (surface)
    SDL_SaveBMP(surface.get(),
                (std::filesystem::path(dir) / (std::string(name) + ".bmp"))
                    .string()
                    .c_str());
  SDL_SetRenderTarget(&r, previous);
}
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected game path");
    auto prefs = std::filesystem::temp_directory_path() /
                 ("summer-system-" + std::to_string(SDL_GetTicksNS()));
    SDL_setenv_unsafe("SUMMER_SAVE_DIR", prefs.string().c_str(), 1);
    auto assets = std::make_shared<SummerRadish::SummerAssets>(argv[1]);
    SummerRadish::SummerRuntime game(assets);
    EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    Engine engine(game, options);
    struct Guard {
      SummerRadish::SummerRuntime &game;
      ~Guard() { game.shutdown(); }
    } guard{game};
    {
      PcmPlayer probe;
      PcmPlayer::Clip clip;
      clip.rate = 48000;
      clip.samples.resize(96000, 1);
      check(
          probe.Play(engine.GetAudio().channel("PauseProbe"), std::move(clip)),
          "PCM probe starts");
      SDL_Delay(30);
      probe.SetPaused(true);
      auto position = probe.Position();
      SDL_Delay(40);
      check(std::abs(probe.Position() - position) < 0.002,
            "PCM pause preserves sample position");
      probe.SetPaused(false);
    }
    click(engine);
    for (int i = 0; i < 4; ++i)
      engine.Iterate();
    check(game.GetState() != GameState::Splash, "Splash exits");
    // Skip warning and logo waits, then press Start and accept uniform A.
    for (int i = 0; i < 8 && !find<ChoiceQuestionScene>(game)->isActive(); ++i)
      press(engine, SDLK_RETURN);
    auto *choice = find<ChoiceQuestionScene>(game);
    check(choice && choice->isActive(), "Uniform selection opens");
    capture(engine, game, "uniform");
    choice->chooseOption(0);
    for (int i = 0; i < 8; ++i)
      press(engine, SDLK_RETURN);
    auto *text = find<VNTextBox>(game);
    check(text && text->isVisible(), "Story dialogue opens");
    check(!text->getText().empty(), "Dialogue has original text");
    capture(engine, game, "story");
    auto save = std::filesystem::temp_directory_path() /
                ("summer-test-" + std::to_string(SDL_GetTicksNS()) + ".vn");
    game.saveGame(save.string());
    auto before = game.debugNode();
    auto line = text->getText();
    press(engine, SDLK_RETURN);
    press(engine, SDLK_RETURN);
    check(game.debugNode() != before, "Advance changes script position");
    game.loadGame(save.string());
    check(game.debugNode() == before && text->getText() == line,
          "Save restores exact dialogue position");
    std::filesystem::remove(save);
    press(engine, SDLK_ESCAPE);
    check(find<InGameMenu>(game)->IsOpen(), "Pause opens");
    auto paused = game.debugNode();
    press(engine, SDLK_RETURN);
    check(game.debugNode() == paused, "Pause consumes confirmation");
    // Exercise the first original choice and its branch after 44 messages.
    for (int i = 0; i < 150 && !choice->isActive(); ++i) {
      text->completeDialogue();
      press(engine, SDLK_RETURN);
    }
    check(choice->isActive() && choice->getOptions().size() == 2,
          "First script choice has two options");
    capture(engine, game, "choice");
    choice->chooseOption(1);
    check(text->isVisible(), "Choice resumes script");
    game.debugJumpToNode("DAY::昼");
    press(engine, SDLK_RETURN);
    check(choice->isActive() && choice->getOptions().size() == 5,
          "Day map opens with five original locations");
    capture(engine, game, "map");
    game.saveGame(save.string());
    auto mapPosition = game.debugNode();
    game.loadGame(save.string());
    check(choice->isActive() && game.debugNode() == mapPosition,
          "Map save restores native choice");
    std::filesystem::remove(save);
    // A loose retail MPEG used to be silently skipped. Follow the final
    // prologue block through PLAYMOVIE and verify that playback suspends the
    // script instead of immediately entering PRORO2.
    check(std::filesystem::is_regular_file(assets->moviePath("SUMMER_OP.MPG")),
          "Loose opening movie resolves case-insensitively");
    SummerRadish::Machine scriptIndex;
    auto prologue = assets->read("SCRIPT/PRORO.XML");
    scriptIndex.add("PRORO", std::string(prologue.begin(), prologue.end()));
    std::size_t moviePC = 0;
    const auto &code = scriptIndex.programs().at("PRORO").code;
    for (std::size_t pc = 0; pc < code.size(); ++pc)
      if (code[pc].node.tag == "FUNCTION" &&
          code[pc].node.attr("name") == "PLAYMOVIE")
        moviePC = pc + 1;
    check(moviePC > 0, "Retail prologue requests its opening movie");
    game.debugJumpToNode("PRORO::Ｈ");
    const auto movieNode = "PRORO:" + std::to_string(moviePC);
    for (int n = 0; n < 500 && game.debugNode() != movieNode; ++n) {
      text->completeDialogue();
      press(engine, SDLK_RETURN);
    }
    check(game.debugNode() == movieNode && !text->isVisible(),
          "Opening movie blocks script execution");
    engine.Iterate();
    capture(engine, game, "opening-movie");
    bool movieSaveRejected = false;
    try {
      game.saveGame(save.string());
    } catch (const std::exception &) {
      movieSaveRejected = true;
    }
    check(movieSaveRejected, "Movie playback is not saved as a dialogue");
    std::cout << "Summer engine title, uniform, dialogue, save/load, pause, "
                 "branch, map and opening movie passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
