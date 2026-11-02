#include "DdlcRuntime.hpp"
#include "DdlcSession.hpp"
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace Kotonoha;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void key(Engine &engine, SDL_Keycode code) {
  SDL_Event e{};
  e.type = SDL_EVENT_KEY_DOWN;
  e.key.key = code;
  engine.Event(&e);
}
void finger(Engine &engine, Uint32 type, SDL_FingerID id, float x = .5f,
            float y = .5f) {
  SDL_Event e{};
  e.type = type;
  e.tfinger.touchID = 7;
  e.tfinger.fingerID = id;
  e.tfinger.x = x;
  e.tfinger.y = y;
  engine.Event(&e);
}
uint64_t dialoguePixels(Engine &engine, Ddlc::GameRuntime &game) {
  auto &r = engine.GetRenderer();
  auto *previous = SDL_GetRenderTarget(&r);
  TexturePtr target(SDL_CreateTexture(&r, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 1280, 720));
  check(bool(target), "Create native capture target");
  SDL_SetRenderTarget(&r, target.get());
  game.render(r, 1);
  SDL_Rect area{260, 550, 760, 120};
  SurfacePtr surface(SDL_RenderReadPixels(&r, &area));
  check(bool(surface), "Read native textbox pixels");
  uint64_t hash = 1469598103934665603ull;
  const auto *bytes = static_cast<const uint8_t *>(surface->pixels);
  for (int y = 0; y < surface->h; ++y)
    for (int x = 0; x < surface->w * 4; ++x)
      hash = (hash ^ bytes[y * surface->pitch + x]) * 1099511628211ull;
  SDL_SetRenderTarget(&r, previous);
  return hash;
}
int main(int argc, char **argv) {
  try {
    if (argc < 2)
      return 77;
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("ddlc-presentation-" + std::to_string(SDL_GetPerformanceCounter()));
    struct Cleanup {
      std::filesystem::path path;
      std::string previous;
      bool set = false;
      ~Cleanup() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        if (set)
          SDL_setenv_unsafe("DDLC_SAVE_DIR", previous.c_str(), 1);
        else
          SDL_unsetenv_unsafe("DDLC_SAVE_DIR");
      }
    } cleanup{directory};
    if (const char *old = SDL_getenv("DDLC_SAVE_DIR")) {
      cleanup.previous = old;
      cleanup.set = true;
    }
    SDL_setenv_unsafe("DDLC_SAVE_DIR", directory.string().c_str(), 1);
    auto assets =
        std::make_shared<Ddlc::GameAssets>(argv[1], argc > 2 ? argv[2] : "");
    Ddlc::GameRuntime game(assets);
    game.SetState(GameState::Gameplay);
    EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    Engine engine(game, options);
    struct Stop {
      Ddlc::GameRuntime &game;
      ~Stop() { game.shutdown(); }
    } stop{game};
    SDL_SetRenderVSync(&engine.GetRenderer(), 0);
    auto *session = game.session();
    check(session && session->playing(), "Native DDLC session starts gameplay");
    auto started = SDL_GetTicks();
    while ((session->dialogueSourceText().empty() ||
            session->dialogueText().empty()) &&
           SDL_GetTicks() - started < 6000) {
      key(engine, SDLK_RETURN);
      engine.Iterate();
      SDL_Delay(1);
    }
    check(!session->dialogueSourceText().empty(), "Native dialogue reached");
    session->completeDialogue();
    const auto source = session->dialogueSourceText(),
               before = session->dialogueText(), node = game.debugNode();
    const auto route = std::to_string(session->translationRoute());
    engine.GetDebug().setVisible(true);
    engine.Iterate();
    const auto originalPixels = dialoguePixels(engine, game);
    size_t streams = 0;
    for (auto &channel : engine.GetAudio().getChannels())
      streams += channel->activeStreams();
    engine.setTranslation(route, source, "{b}Edição ao vivo{/b}: [1 + 1].");
    check(session->dialogueText().find("Edição ao vivo") != std::string::npos &&
              session->dialogueText().find("2.") != std::string::npos,
          "Live native text re-interpolates variables and preserves tags");
    check(dialoguePixels(engine, game) != originalPixels,
          "Live translation changes actual native textbox pixels");
    check(session->dialogueComplete(),
          "Live edit keeps completed typewriter state");
    check(game.debugNode() == node, "Live edit does not advance VM");
    size_t after = 0;
    for (auto &channel : engine.GetAudio().getChannels())
      after += channel->activeStreams();
    check(after == streams, "Live edit does not recreate audio streams");
    engine.Iterate();
    VNTextBox *box = nullptr;
    InGameMenu *pause = nullptr;
    for (auto *scene : game.GetActiveScenes()) {
      if (auto *b = dynamic_cast<VNTextBox *>(scene))
        box = b;
      if (auto *p = dynamic_cast<InGameMenu *>(scene))
        pause = p;
    }
    check(box && box->getText() == session->dialogueText(),
          "Native VNTextBox exposes the edited rendered text");
    engine.setTranslation(route, source, "Outra edição.");
    check(session->dialogueText().find("Outra edição.") != std::string::npos,
          "Repeated live edits use original binding");
    engine.setTranslation(route, source, "");
    check(session->dialogueText() == before,
          "Clearing live edit restores native source");
    engine.GetDebug().setVisible(false);
    engine.Iterate();
    check(pause, "Native pause scene exists");
    pause->open();
    check(session->paused() && pause->IsOpen(),
          "Core pause open delegates to native session");
    key(engine, SDLK_ESCAPE);
    check(!session->paused(), "Back closes native pause");
    key(engine, SDLK_ESCAPE);
    check(session->paused(), "Back opens native pause");
    key(engine, SDLK_ESCAPE);
    check(!session->paused(), "Back toggles native pause once");
    finger(engine, SDL_EVENT_FINGER_DOWN, 1);
    finger(engine, SDL_EVENT_FINGER_DOWN, 2);
    check(game.debugNode() == node, "Touch down never advances native story");
    finger(engine, SDL_EVENT_FINGER_UP, 1);
    finger(engine, SDL_EVENT_FINGER_UP, 2);
    check(session->paused() && game.debugNode() == node,
          "Two-finger back opens native pause without advance");
    key(engine, SDLK_ESCAPE);
    key(engine, SDLK_B);
    check(session->historyOpen(), "Backlog key opens native History");
    key(engine, SDLK_ESCAPE);
    key(engine, SDLK_ESCAPE);
    check(!session->paused(), "Back exits native History then pause");
    // A real native menu exercises display labels while retaining branch
    // values.
    bool testedChoice = false;
    for (const auto &name : assets->script().labelNames()) {
      auto *label = assets->script().label(name);
      if (label->blocks.empty() || label->blocks[0].empty() ||
          label->blocks[0][0]->k != Ddlc::Node::K::Menu)
        continue;
      session->debugJump(name);
      engine.Iterate();
      auto labels = session->choiceTexts();
      if (labels.empty())
        continue;
      const auto choiceNode = game.debugNode();
      engine.setTranslation(std::to_string(session->translationRoute()),
                            labels.front(), "Escolha ao vivo");
      check(session->choiceTexts().front() == "Escolha ao vivo",
            "Native choice label updates immediately");
      check(game.debugNode() == choiceNode,
            "Choice live edit does not answer selection");
      testedChoice = true;
      break;
    }
    check(testedChoice, "Native choice fixture found");
    std::cout << "Native DDLC live dialogue, choices, pause/back/history and "
                 "touch passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
