#include "AirRuntime.hpp"
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
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
void capture(Engine &engine, Runtime &game, const char *name) {
  auto &r = engine.GetRenderer();
  TexturePtr target(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 960, 720));
  SDL_SetRenderTarget(&r, target.get());
  game.render(r, 1);
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
    Engine engine(game, o);
    struct Guard {
      Air::AirRuntime &g;
      ~Guard() { g.shutdown(); }
    } guard{game};
    SDL_SetRenderVSync(&engine.GetRenderer(), 0);
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
    // Debug entry after the long animated date introduction isolates text/VM
    // tests.
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
    t->completeDialogue();
    const auto node = game.debugNode(), line = t->getText();
    auto path = std::filesystem::temp_directory_path() /
                ("air-test-" + std::to_string(SDL_GetTicksNS()) + ".vn");
    game.saveGame(path.string());
    game.debugJumpToNode("9032");
    game.loadGame(path.string());
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
    menu.close();
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
    for (const auto &entry : {"170:2441", "173:14", "163:1373", "702:25244"}) {
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
