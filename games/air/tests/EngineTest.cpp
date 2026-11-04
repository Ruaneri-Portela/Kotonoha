#include "AirRuntime.hpp"
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/media/SurfaceBlend.hpp>
#include <algorithm>
#include <iostream>
using namespace Kotonoha;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
VNTextBox *text(Runtime &game) {
  for (auto *s : game.GetActiveScenes())
    if (auto *t = dynamic_cast<VNTextBox *>(s))
      return t;
  return nullptr;
}
SDL_Event pad(Uint8 button, bool down) {
  SDL_Event event{};
  event.type =
      down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
  event.gbutton.button = button;
  event.gbutton.down = down;
  return event;
}
void capture(Engine &engine, Runtime &game, const char *name) {
  auto &r = engine.GetRenderer();
  TexturePtr target(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 960, 720));
  SDL_SetRenderTarget(&r, target.get());
  game.render(r, 1);
  game.renderOverlay(r, 1);
  SurfacePtr s(SDL_RenderReadPixels(&r, nullptr));
  check(bool(s), "Capture failed");
  SDL_SaveBMP(s.get(), name);
  SDL_SetRenderTarget(&r, nullptr);
}
int main(int argc, char **argv) {
  try {
    if (argc < 2 || !argv[1][0])
      return 77;
    auto assets = std::make_shared<Air::AirAssets>(argv[1]);
    // SEEN8613 loads MSG_01 without blending, then reveals rows with maskCopy.
    // The intermediate copy must keep the mask, including zero-alpha RGB.
    {
      auto image = assets->loadSurface(assets->graphic("MSG_01"));
      SurfacePtr buffer(
          SDL_CreateSurface(image->w, image->h, SDL_PIXELFORMAT_RGBA32));
      SurfacePtr row(SDL_CreateSurface(500, 30, SDL_PIXELFORMAT_RGBA32));
      BlitSurfaceOpacity(*image, *buffer, nullptr, nullptr, 255, false);
      for (int opacity : {0, 128, 255}) {
        SDL_FillSurfaceRect(row.get(), nullptr,
                            SDL_MapSurfaceRGBA(row.get(), 255, 255, 255, 255));
        SDL_Rect source{70, 0, 500, 30};
        BlitSurfaceOpacity(*buffer, *row, &source, nullptr, opacity, true);
        int white = 0, ink = 0;
        for (int y = 0; y < 30; ++y)
          for (int x = 0; x < 500; ++x) {
            Uint8 r, g, b, a;
            SDL_ReadSurfacePixel(row.get(), x, y, &r, &g, &b, &a);
            white += r > 245 && g > 245 && b > 245;
            ink += r < 200 && g < 200 && b < 200;
          }
        check(white > 10000, "AIR intro mask must preserve the white "
                             "background throughout the fade");
        if (opacity == 255)
          check(ink > 20, "AIR intro row must contain readable text");
      }
    }
    Air::AirRuntime game(assets);
    auto sources = game.translationSources();
    std::size_t translationCount = 0;
    for (const auto &[route, texts] : sources)
      translationCount += texts.size();
    check(sources.size() == 96 && translationCount > 40000,
          "AIR translation corpus was not exported");
    check(std::find(sources.at(170).begin(), sources.at(170).end(),
                    "Eu tava deslizando pelo vento...") !=
              sources.at(170).end(),
          "AIR translation route/source changed");
    const auto dictionaryPath =
        std::filesystem::temp_directory_path() /
        ("air-translation-test-" + std::to_string(SDL_GetTicksNS()) + ".json");
    Translation exported;
    exported.dump(dictionaryPath.string(), sources);
    Translation loaded;
    loaded.load(dictionaryPath.string());
    std::filesystem::remove(dictionaryPath);
    if (argc > 2 && std::string(argv[2]) == "--corpus-only") {
      std::cout << "Translation routes=" << sources.size()
                << " entries=" << translationCount << '\n';
      return 0;
    }
    game.SetState(GameState::Menu);
    EngineOptions o;
    o.assets = assets;
    o.persistSettings = false;
    const bool gpuTest = argc > 2 && std::string(argv[2]) == "--ui-opengl";
    if (gpuTest) {
      o.rendererOverride = "opengl";
      o.settings.upscaling = {
          UpscaleBackend::MPV, UpscalePreset::Anime4KModeCFast, 2, {}};
    }
    Engine engine(game, o);
    struct Guard {
      Air::AirRuntime &g;
      ~Guard() { g.shutdown(); }
    } guard{game};
    SDL_SetRenderVSync(&engine.GetRenderer(), 0);
    if (argc > 2 && std::string(argv[2]) == "--intro-capture") {
      game.debugJumpToNode(argc > 3 ? argv[3] : "8993:97");
      for (int n = 0; n < 450; ++n) {
        engine.Iterate();
        if (n % 30 == 0) {
          const auto name = "/tmp/air-intro-" + std::to_string(n) + ".bmp";
          capture(engine, game, name.c_str());
          std::cout << n << " " << game.debugNode() << std::endl;
        }
        SDL_Delay(16);
      }
      return 0;
    }

    // A native refresh presents a complete script frame before executing
    // the next loop iteration, rather than stopping at an arbitrary VM budget.
    game.debugJumpToNode("9032:859");
    game.update();
    check(game.debugNode() == "9032:860",
          "AIR refresh did not yield at the frame boundary");
    game.debugJumpToNode("9037:118");
    game.update();
    SDL_Event typed{};
    typed.type = SDL_EVENT_TEXT_INPUT;
    typed.text.text = "AIR";
    game.onEvent(typed);
    capture(engine, game, "/tmp/air-name-fields.bmp");
    SurfacePtr fields(SDL_LoadBMP("/tmp/air-name-fields.bmp"));
    check(bool(fields), "Name field capture failed");
    int dark = 0, white = 0;
    for (int y = 201; y < 253; ++y)
      for (int x = 388; x < 592; ++x) {
        Uint8 r, g, b, a;
        SDL_ReadSurfacePixel(fields.get(), x, y, &r, &g, &b, &a);
        dark += r < 80 && g < 80 && b < 80;
        white += r > 240 && g > 240 && b > 240;
      }
    check(dark > 20 && white > 8000,
          "Name fields must render dark text on a white background");
    // Returning to the title must discard every object and edit control
    // belonging to the previous scene before running the title script.
    auto &returnMenu = GetInGameMenuRuntime(engine);
    game.SetState(GameState::Gameplay);
    // AIR registers the shared scenes for gameplay by reusing the menu scenes;
    // mirror that here and keep the menu list intact for the return to title.
    if (game.GetScenes(GameState::Gameplay).empty())
      for (auto *scene : game.GetScenes(GameState::Menu))
        game.AddScene(scene, GameState::Gameplay);
    game.debugJumpToNode("170:246");
    game.update();
    check(engine.GetAudio().channel("BGM")->activeStreams() > 0,
          "Scene music starts before testing return to title");
    // Start opens the pause menu, South cancels it (even with dialogue on
    // screen), and the D-pad plus East reach "Main Menu".
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_START, true));
    check(returnMenu.IsOpen(), "Gamepad Start must open the pause menu");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_START, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(!returnMenu.IsOpen(),
          "Gamepad South must cancel the pause menu while dialogue is visible");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_START, true));
    check(returnMenu.IsOpen(), "Gamepad Start must reopen the pause menu");
    for (int i = 0; i < 5; ++i) {
      game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
      game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    }
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, true));
    check(game.GetState() == GameState::Menu && game.debugNode() == "9032:0",
          "Gamepad East must activate Main Menu and reset the script");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, false));
    check(
        engine.GetAudio().channel("BGM")->activeStreams() == 0,
        "Returning to title stops scene music before the native NAMI ambience");
    capture(engine, game, "/tmp/air-clean-title.bmp");
    SurfacePtr clean(SDL_LoadBMP("/tmp/air-clean-title.bmp"));
    for (int y = 0; y < clean->h; y += 16)
      for (int x = 0; x < clean->w; x += 16) {
        Uint8 r, g, b, a;
        SDL_ReadSurfacePixel(clean.get(), x, y, &r, &g, &b, &a);
        check(r == 0 && g == 0 && b == 0,
              "Previous scene leaked into the title canvas");
      }
    game.debugJumpToNode("9032:911");
    game.update();
    VNSaveMenu *loadMenu = nullptr;
    for (auto *scene : game.GetActiveScenes())
      if (auto *menu = dynamic_cast<VNSaveMenu *>(scene))
        loadMenu = menu;
    check(game.diagnostic().empty() && loadMenu && loadMenu->isOpen(),
          "AIR menu_load at 9032:911 must open the load menu");
    const auto loadNode = game.debugNode();
    game.update();
    check(game.debugNode() == loadNode,
          "AIR load menu must pause script execution");
    loadMenu->close();
    if (gpuTest || (argc > 2 && std::string(argv[2]) == "--ui-only")) {
      std::cout
          << "AIR name-field contrast and refresh frame boundary passed\n";
      return 0;
    }
    game.debugJumpToNode("9030");
    // Run the original boot path, including its timed logo frames.
    auto start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 30000) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      if (game.debugNode().find("9032:") == 0 && SDL_GetTicks() - start > 15000)
        break;
      SDL_Delay(1);
    }
    check(game.debugNode().find("9032:") == 0,
          "Original startup did not reach title");
    capture(engine, game, "/tmp/air-title.bmp");
    const auto titleReady = SDL_GetTicks();
    while (game.debugNode() != "9032:860" &&
           SDL_GetTicks() - titleReady < 15000) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      SDL_Delay(1);
    }
    check(game.debugNode() == "9032:860",
          "Original title input loop must be ready");
    // The title script only polls mouse registers. The runtime maps the D-pad
    // to a virtual cursor over the native hitboxes and turns Enter/East into a
    // press/release pair, so the title is playable without a pointer.
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
    // Entry 1 is Load. Activating it with the gamepad proves the mapped East
    // button reaches the same native press/release path.
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, true));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_EAST, false));
    VNSaveMenu *titleLoad = nullptr;
    for (int n = 0; n < 600 && !titleLoad; ++n) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      for (auto *scene : game.GetActiveScenes())
        if (auto *menu = dynamic_cast<VNSaveMenu *>(scene))
          if (menu->isOpen())
            titleLoad = menu;
    }
    check(titleLoad, "Gamepad East must activate the AIR title Load entry");
    titleLoad->close();
    for (int n = 0; n < 600 && game.debugNode() != "9032:860"; ++n) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
    }
    check(game.debugNode() == "9032:860",
          "AIR title must return to its input loop after closing Load");
    SDL_Event titleUp{};
    titleUp.type = SDL_EVENT_KEY_DOWN;
    titleUp.key.key = SDLK_UP;
    titleUp.key.down = true;
    game.onEvent(titleUp);
    SDL_Event titleConfirm{};
    titleConfirm.type = SDL_EVENT_KEY_DOWN;
    titleConfirm.key.key = SDLK_RETURN;
    titleConfirm.key.down = true;
    game.onEvent(titleConfirm);
    for (int n = 0; n < 600 && game.debugNode() == "9032:860"; ++n) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
    }
    check(game.debugNode() != "9032:860",
          "Keyboard Enter must activate the original AIR title button after "
          "the gamepad moved its virtual cursor");
    std::cout << "AIR title keyboard/gamepad activation passed at "
              << game.debugNode() << '\n';

    // Execute the native date/title setup, then skip its long animation so
    // this test exercises the save UI rather than waiting through the intro.
    game.debugJumpToNode("170:0");
    game.update();
    check(std::string(SDL_GetWindowTitle(&engine.GetWindow()))
                  .find("17 de Julho (Seg)") != std::string::npos,
          "The native script must set the save date title");
    game.debugJumpToNode("170:16");
    start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 6000 &&
           (!text(game) || !text(game)->isVisible())) {
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      SDL_Delay(1);
    }
    auto *t = text(game);
    check(t && t->isVisible(), "Dialogue did not open");
    check(t->getText().find("Eu tava deslizando pelo vento...") !=
              std::string::npos,
          "Original Portuguese text was not preserved");
    auto nativeButton = [&](float x, float y) {
      SDL_Event event{};
      event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      event.button.which = SDL_TOUCH_MOUSEID;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.x = x;
      event.button.y = y;
      check(t->handleEvent(event), "AIR native control accepts finger-down");
      event.type = SDL_EVENT_MOUSE_BUTTON_UP;
      check(t->handleEvent(event), "AIR native control accepts finger-up");
    };
    nativeButton(622, 364);
    check(GetVNBacklogRuntime(engine).isVisible(),
          "AIR original back button opens the backlog");
    GetVNBacklogRuntime(engine).close();
    auto &backlog = GetVNBacklogRuntime(engine);
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    check(backlog.isVisible(),
          "Gamepad left shoulder must open the AIR history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_BACK, true));
    check(!backlog.isVisible(), "Gamepad Back must close the AIR history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_BACK, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    check(backlog.isVisible(), "Gamepad left shoulder must reopen the history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false));
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, true));
    check(!backlog.isVisible(), "Gamepad South must close the AIR history");
    game.onEvent(pad(SDL_GAMEPAD_BUTTON_SOUTH, false));
    nativeButton(600, 352);
    SDL_Event reveal{};
    reveal.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    reveal.button.button = SDL_BUTTON_LEFT;
    reveal.button.x = reveal.button.y = 75;
    const auto hiddenNode = game.debugNode();
    game.onEvent(reveal);
    check(game.debugNode() == hiddenNode,
          "Restoring hidden AIR text must not advance the story");
    nativeButton(622, 402);
    t->completeDialogue();
    const auto node = game.debugNode(), line = t->getText();
    auto path = std::filesystem::temp_directory_path() /
                ("air-test-" + std::to_string(SDL_GetTicksNS()) + ".vn");
    const auto namedDirectory = path.string() + "-names";
    VNSaveMenu namedSave(engine);
    game.AddScene(&namedSave);
    namedSave.open(namedDirectory, false, "17 de Julho (Seg)");
    SDL_Event confirmName{};
    confirmName.type = SDL_EVENT_KEY_DOWN;
    confirmName.key.key = SDLK_RETURN;
    namedSave.onEvent(confirmName);
    TextInputDialog *nameDialog = nullptr;
    for (auto *scene : game.GetActiveScenes())
      if (auto *dialog = dynamic_cast<TextInputDialog *>(scene))
        nameDialog = dialog;
    check(nameDialog, "New save must show the editable novel suggestion");
    nameDialog->onEvent(confirmName);
    check(!namedSave.isOpen() && std::filesystem::is_regular_file(
                                     std::filesystem::path(namedDirectory) /
                                     "17 de Julho (Seg).vn"),
          "Accepting the suggestion must create a named AIR save");
    game.RemoveScene(&namedSave);
    std::filesystem::remove_all(namedDirectory);
    game.saveGame(path.string());
    check(VNSave::read(path.string()).gameData.find("17 de Julho (Seg)") !=
              std::string::npos,
          "AIR must save the script-provided date title");
    auto legacy = VNSave::read(path.string());
    const auto titlePosition = legacy.gameData.rfind("\"17 de Julho (Seg)\"");
    check(titlePosition != std::string::npos, "AIR date must be serialized");
    legacy.gameData.erase(titlePosition);
    legacy.write(path.string());
    game.debugJumpToNode("9032");
    game.loadGame(path.string());
    game.saveGame(path.string());
    check(VNSave::read(path.string()).gameData.find("17 de Julho (Seg)") !=
              std::string::npos,
          "Older saves recover the date from the original scenario title");
    check(game.debugNode() == node && text(game)->getText() == line,
          "Save restored wrong script or dialogue");
    std::filesystem::remove(path);
    capture(engine, game, "/tmp/air-story.bmp");
    auto &menu = GetInGameMenuRuntime(engine);
    menu.open();
    auto paused = game.debugNode();
    for (int n = 0; n < 10; ++n)
      engine.Iterate();
    check(game.debugNode() == paused, "Pause menu advanced story");
    auto touch = [&](Uint32 type, int id) {
      SDL_Event event{};
      event.type = type;
      event.tfinger.touchID = 1;
      event.tfinger.fingerID = id;
      event.tfinger.x = 0.5f + id * 0.02f;
      event.tfinger.y = 0.5f;
      engine.Event(&event);
    };
    touch(SDL_EVENT_FINGER_DOWN, 1);
    touch(SDL_EVENT_FINGER_DOWN, 2);
    touch(SDL_EVENT_FINGER_UP, 1);
    touch(SDL_EVENT_FINGER_UP, 2);
    check(!menu.IsOpen(), "Two-finger touch did not close AIR pause menu");
    const auto progressStart = SDL_GetTicks();
    std::string lastNode = game.debugNode();
    int progress = 0;
    for (int n = 0;
         n < 1200 && SDL_GetTicks() - progressStart < 40000 && progress < 500;
         ++n) {
      if (text(game))
        text(game)->completeDialogue();
      SDL_Event event{};
      event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.x = 75;
      event.button.y = 75;
      game.onEvent(event);
      event.type = SDL_EVENT_MOUSE_BUTTON_UP;
      game.onEvent(event);
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      if (game.debugNode() != lastNode) {
        lastNode = game.debugNode();
        ++progress;
      }
      SDL_Delay(10);
    }
    std::cout << "Story progress transitions=" << progress << " at "
              << game.debugNode() << '\n';
    check(game.debugNode() != node, "Restored dialogue could not advance");
    // Reproduce the reported live block and exercise multi-image transitions.
    for (const auto &entry :
         {"170:2441", "173:14", "163:1373", "702:25244", "9037:118"}) {
      game.debugJumpToNode(entry);
      engine.Iterate();
      check(game.diagnostic().empty(), game.diagnostic().c_str());
      if (game.debugNode() == entry)
        throw std::runtime_error(
            std::string("Regression command did not execute at ") + entry);
    }

    std::cout << "Original startup/title, Portuguese dialogue, save/load and "
                 "pause passed at "
              << game.debugNode() << '\n';
  } catch (std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
