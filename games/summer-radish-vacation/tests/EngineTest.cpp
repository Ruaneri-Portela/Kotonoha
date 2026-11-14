#include "SummerBacklog.hpp"
#include "SummerRuntime.hpp"
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/TextInputForm.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/PcmPlayer.hpp>
#include <SummerRadish/Script.hpp>
#include <algorithm>
#include <fstream>
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
    std::filesystem::create_directories(prefs);
    {
      std::ofstream saved(prefs / "system.dat", std::ios::binary);
      saved << "SUMMER-SYSTEM-1 2\n"
               "\"$Valid\" 0 \"42\"\n"
               "\"$Invalid\" 0 \"not a number\"\n";
    }
    auto assets = std::make_shared<Kotonoha::Providers::SummerAssets>(argv[1]);
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
    bool renamed = false;
    for (int i = 0; i < 12 && !find<ChoiceQuestionScene>(game)->isActive();
         ++i) {
      if (auto *form = find<TextInputForm>(game)) {
        const auto inState = [&](GameState state) {
          const auto &scenes = game.GetScenes(state);
          return std::find(scenes.begin(), scenes.end(), form) != scenes.end();
        };
        check(inState(GameState::Menu) && inState(GameState::Gameplay),
              "Native name entry overlays the main scene in every state");
        const auto position = game.debugNode();
        form->submit({"", "瞬", "はじめ", "しゅん"});
        check(form->capturesInput() && game.debugNode() == position,
              "Invalid native name must not advance the script");
        form->submit({"試験", "花子", "しけん", "はなこ"});
        check(!form->capturesInput(), "Valid native names commit atomically");
        renamed = true;
      } else
        press(engine, SDLK_RETURN);
    }
    check(renamed, "Native NAMEENTRY was skipped");
    auto *choice = find<ChoiceQuestionScene>(game);
    check(choice && choice->isActive(), "Uniform selection opens");
    capture(engine, game, "uniform");
    choice->chooseOption(0);
    for (int i = 0; i < 8; ++i)
      press(engine, SDLK_RETURN);
    auto *text = find<VNTextBox>(game);
    check(text && text->isVisible(), "Story dialogue opens");
    check(!text->getText().empty(), "Dialogue has original text");
    const auto &gameplay = game.GetScenes(GameState::Gameplay);
    for (auto *scene : gameplay)
      check(std::count(gameplay.begin(), gameplay.end(), scene) == 1,
            "GAMESTART never registers the same scene twice");
    capture(engine, game, "story");
    auto padButton = [&](SDL_GamepadButton button, bool down) {
      SDL_Event event{};
      event.type =
          down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
      event.gbutton.which = 777;
      event.gbutton.button = button;
      event.gbutton.down = down;
      engine.Event(&event);
    };
    auto tapPad = [&](SDL_GamepadButton button) {
      padButton(button, true);
      padButton(button, false);
    };
    // Raw directions must reach the native toolbar even though dialogue maps
    // Left/Right to text speed. Four steps land on the system control.
    for (int i = 0; i < 4; ++i)
      tapPad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    tapPad(SDL_GAMEPAD_BUTTON_EAST);
    check(GetInGameMenuRuntime(engine).IsOpen(),
          "Controller focuses and activates the message-box toolbar");
    GetInGameMenuRuntime(engine).close();
    // Up releases the toolbar so confirmation advances the novel again.
    text->completeDialogue();
    const auto toolbarNode = game.debugNode();
    const auto toolbarLine = text->getText();
    tapPad(SDL_GAMEPAD_BUTTON_DPAD_UP);
    tapPad(SDL_GAMEPAD_BUTTON_EAST);
    check(game.debugNode() != toolbarNode || text->getText() != toolbarLine,
          "Up releases the toolbar and confirmation reaches the novel");
    auto nativeButton = [&](float x, float y) {
      SDL_Event event{};
      event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      event.button.which = SDL_TOUCH_MOUSEID;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.x = x;
      event.button.y = y;
      check(text->handleEvent(event),
            "Summer native control accepts touch-down");
      event.type = SDL_EVENT_MOUSE_BUTTON_UP;
      check(text->handleEvent(event), "Summer native control accepts touch-up");
    };
    nativeButton(770, 540);
    check(std::filesystem::is_regular_file(prefs / "quicksave.vn"),
          "Original quicksave control writes a save");
    nativeButton(550, 585);
    check(GetVNBacklogRuntime(engine).isVisible(),
          "Original backlog control opens history");
    auto *history = dynamic_cast<SummerRadish::SummerBacklog *>(
        &GetVNBacklogRuntime(engine));
    check(history, "Summer installs its original history UI");
    const auto historyPosition = game.debugNode();
    capture(engine, game, "backlog");
    SDL_Event historyEvent{};
    historyEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    historyEvent.button.which = SDL_TOUCH_MOUSEID;
    historyEvent.button.button = SDL_BUTTON_LEFT;
    historyEvent.button.x = 770;
    historyEvent.button.y = 85;
    const auto last = history->selectedEntry();
    history->onEvent(historyEvent);
    historyEvent.type = SDL_EVENT_MOUSE_BUTTON_UP;
    history->onEvent(historyEvent);
    check(history->selectedEntry() == (last > 3 ? last - 3 : 0),
          "Original fast rewind traverses three entries");
    historyEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    historyEvent.button.y = 560;
    history->onEvent(historyEvent);
    historyEvent.type = SDL_EVENT_MOUSE_BUTTON_UP;
    history->onEvent(historyEvent);
    check(!history->isVisible() && game.debugNode() == historyPosition,
          "Native return closes history without advancing the story");
    nativeButton(730, 585);
    check(GetInGameMenuRuntime(engine).IsOpen(),
          "Original system control opens its menu");
    GetInGameMenuRuntime(engine).close();

    auto save = std::filesystem::temp_directory_path() /
                ("summer-test-" + std::to_string(SDL_GetTicksNS()) + ".vn");
    game.saveGame(save.string());
    const auto savedData = VNSave::read(save.string()).gameData;
    check(savedData.find("$ChangeName00") != std::string::npos &&
              savedData.find("試験") != std::string::npos,
          "Native player names are included in the save");
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
    check(choice->getOptions().front().hitBox.h == 42 &&
              choice->getOptions().front().hitBox.y == 254,
          "Summer choices use native 42-pixel strips centered with 8-pixel "
          "spacing");
    const auto choiceSave = prefs / "native-choice.vn";
    game.saveGame(choiceSave.string());
    choice->chooseOption(1);
    game.loadGame(choiceSave.string());
    check(choice->isActive() && choice->getOptions().size() == 2,
          "A saved native choice restores its visibility and original option "
          "IDs");
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
    check(assets->moviePath("DISC1/opening.mpg") ==
              assets->moviePath("SUMMER_OP.MPG"),
          "DISC1 opening alias resolves the installed summer_op.mpg");
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
