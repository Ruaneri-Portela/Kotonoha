// Snow Radish Vacation!! on the Kotonoha engine, against the user's DAT
// directory: the AssetProvider and the polymorphic components through their
// engine base classes (logo -> title -> story -> menus -> choice -> save / load).
#include "SnowAssets.hpp"
#include "SnowLauncher.hpp"
#include "SnowRuntime.hpp"

#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/localization/Translation.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using Kotonoha::GameState;

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename T> T *find(Kotonoha::Runtime &runtime) {
  for (auto *scene : runtime.GetActiveScenes())
    if (auto *typed = dynamic_cast<T *>(scene)) return typed;
  return nullptr;
}

void send(Kotonoha::Engine &engine, SDL_Event event) {
  event.common.timestamp = SDL_GetTicksNS();
  engine.Event(&event);
  engine.Iterate();
}

void press(Kotonoha::Engine &engine, SDL_Keycode key) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.windowID = SDL_GetWindowID(&engine.GetWindow());
  event.key.key = key;
  event.key.down = true;
  send(engine, event);
}

// A left click at an 800x600 game position (960x720 viewport = x * 1.2, y * 1.2).
void click(Kotonoha::Engine &engine, float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.windowID = SDL_GetWindowID(&engine.GetWindow());
  event.motion.x = x * 1.2f;
  event.motion.y = y * 1.2f;
  send(engine, event);
  event = {};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.windowID = SDL_GetWindowID(&engine.GetWindow());
  event.button.button = SDL_BUTTON_LEFT;
  event.button.down = true;
  event.button.clicks = 1;
  event.button.x = x * 1.2f;
  event.button.y = y * 1.2f;
  send(engine, event);
}

void frames(Kotonoha::Engine &engine, int count) {
  for (int i = 0; i < count; ++i) {
    engine.Iterate();
    SDL_Delay(5);
  }
}

// SNOW_CAPTURE_DIR=dir: saves the novel space (the 960x720 viewport) of the current frame as BMP.
void capture(Kotonoha::Engine &engine, Kotonoha::Runtime &game, const char *name) {
  const char *dir = SDL_getenv("SNOW_CAPTURE_DIR");
  if (!dir || !*dir) return;
  SDL_Renderer *r = &engine.GetRenderer();
  SDL_Texture *target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, game.viewport().x,
                                           game.viewport().y);
  require(target != nullptr, SDL_GetError());
  SDL_Texture *previous = SDL_GetRenderTarget(r);
  SDL_SetRenderTarget(r, target);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  SDL_RenderClear(r);
  game.render(*r, 1.0f);
  game.renderScreen(*r, 1.0f);
  if (SDL_Surface *pixels = SDL_RenderReadPixels(r, nullptr)) {
    SDL_SaveBMP(pixels, (std::string(dir) + "/" + name + ".bmp").c_str());
    SDL_DestroySurface(pixels);
  }
  SDL_SetRenderTarget(r, previous);
  SDL_DestroyTexture(target);
}

// The engine splash ignores keys and ends on a click (or touch).
// Dumps the translation routes and checks that `shown` (a page on screen) and
// route `route` are in the file.
void testDump(Kotonoha::Engine &engine, const std::string &shown, int route) {
  const auto path = std::filesystem::temp_directory_path() / ("dump-" + std::to_string(SDL_GetTicksNS()) + ".json");
  engine.dumpTranslation(path.string());
  Kotonoha::Translation reloaded;
  reloaded.load(path.string());
  std::ifstream file(path, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(file)), {});
  file.close();
  std::filesystem::remove(path);
  // A JSON-safe start of the page: up to 3 characters of its first line.
  std::string snippet;
  for (size_t i = 0, chars = 0; i < shown.size() && chars < 3; ++chars) {
    size_t end = i + 1;
    while (end < shown.size() && (uint8_t(shown[end]) & 0xC0) == 0x80) ++end;
    const std::string c = shown.substr(i, end - i);
    if (c == "\"" || c == "\\" || c == "/" || uint8_t(c[0]) < 0x20) break;
    snippet += c;
    i = end;
  }
  require(!snippet.empty(), "The page has text");
  require(json.find("\"" + std::to_string(route) + "\"") != std::string::npos, "Dump has the route " + std::to_string(route));
  require(json.find(snippet) != std::string::npos, "Dump has the page text: " + snippet);
  std::cout << "translation dump: " << json.size() << " bytes\n";
}

void skipSplash(Kotonoha::Engine &engine, Kotonoha::Runtime &game) {
  require(game.GetState() == GameState::Splash, "Starts at the engine splash");
  frames(engine, 3);
  press(engine, SDLK_RETURN);
  frames(engine, 2);
  require(game.GetState() == GameState::Splash, "Keys do not skip the splash");
  capture(engine, game, "splash");
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.windowID = SDL_GetWindowID(&engine.GetWindow());
  event.button.button = SDL_BUTTON_LEFT;
  event.button.down = true;
  event.button.clicks = 1;
  event.button.x = 100;
  event.button.y = 100;
  send(engine, event);
  frames(engine, 2);
  require(game.GetState() == GameState::Menu, "A click skips the splash");
}

void testProvider(SnowRadish::SnowAssets &assets) {
  require(assets.contains("script") && !assets.Script().empty(), "SCRIPT.DAT is readable");
  require(assets.contains("graphic/logo00.evd"), "The logo picture is in GRAPHIC.DAT");
  require(!assets.contains("graphic/not_a_picture.evd"), "Unknown pictures are rejected");
  auto logo = assets.loadSurface("graphic/logo00.evd");
  require(logo && logo->w == 800 && logo->h == 600, "EVD decodes to an 800x600 surface");
  auto sprite = assets.loadSurface("graphic/wakuh.chd");
  require(sprite && sprite->w > 0 && assets.Sprite("wakuh").valid, "CHD decodes with its placement");
  const auto bgm = assets.loadAudio("sound/sr001");
  require(bgm.spec.freq == 22050 && bgm.spec.channels == 2 && !bgm.data.empty(), "ADPS decodes as stereo PCM");
  require(!assets.list("graphic/").empty(), "Provider lists the pictures");
}

void testGame(const std::string &directory) {
  auto assets = std::make_shared<SnowRadish::SnowAssets>(directory);
  testProvider(*assets);

  const auto saves = std::filesystem::temp_directory_path() / ("snow-saves-" + std::to_string(SDL_GetTicksNS()));
  SDL_setenv_unsafe("SNOW_SAVE_DIR", saves.string().c_str(), 1);
  SnowRadish::SnowRuntime game(assets);
  {
    Kotonoha::EngineOptions options;
    options.assets = assets;
    Kotonoha::Engine engine(game, options);
    // The game owns audio streams and textures: release them before the engine on any exit.
    struct ShutdownGuard {
      SnowRadish::SnowRuntime &game;
      ~ShutdownGuard() { game.Shutdown(); }
    } guard{game};
    require(&engine.GetAssets() == assets.get(), "The DAT directory is the engine AssetProvider");
    skipSplash(engine, game);
    auto *title = find<Kotonoha::GameMenu>(game);
    require(title && title->isVisible(), "Title is a visible GameMenu");
    frames(engine, 10);
    capture(engine, game, "0-logo");
    for (int i = 0; i < 400 && !game.Session()->TitleShown(); ++i) {
      click(engine, 10, 10); // skips the logo animation and the fades
      frames(engine, 4);
    }
    require(game.Session()->TitleShown(), "Logo -> white -> title");
    frames(engine, 2);
    capture(engine, game, "0-title");

    // Logo -> white -> title; "New game" (no saves: 0x144, 0x170) through GameMenu::activate.
    SnowRadish::SnowSession &session = *game.Session();
    for (int i = 0; i < 400 && game.GetState() != GameState::Gameplay; ++i) {
      click(engine, 0x144 + 40, 0x170 + 20);
      frames(engine, 4);
    }
    require(game.GetState() == GameState::Gameplay && session.Playing(), "New game starts the story");
    std::cout << "story started\n";

    auto *text = find<Kotonoha::VNTextBox>(game);
    auto *menu = find<Kotonoha::InGameMenu>(game);
    auto *backlog = find<Kotonoha::VNBacklog>(game);
    int choiceScenes = 0;
    for (auto *scene : game.GetActiveScenes())
      if (dynamic_cast<Kotonoha::ChoiceQuestionScene *>(scene)) ++choiceScenes;
    require(text && menu && backlog && choiceScenes == 3, "Gameplay uses the engine components");

    // Only advance once the screen settles, otherwise Return skips the page being faded in.
    for (int i = 0; i < 2000 && !(session.HasMessage() && !session.Fading() && !session.Choosing()); ++i) {
      // Text choices start unfocused (as in the original): Down focuses the first option.
      if (session.Choosing() && session.Focus() < 0) press(engine, SDLK_DOWN);
      if (!session.Fading()) press(engine, SDLK_RETURN);
      frames(engine, 1);
    }
    require(session.HasMessage() && !session.Fading(), "Timed out: first message");
    frames(engine, 2);
    require(text->isVisible() && text->getText() == session.Text(), "VNTextBox follows the script page");
    {
      const auto node = engine.GetDebug().nodeClockText();
      std::cout << node << '\n';
      require(node.rfind("Node: ", 0) == 0 && node.find(".obj") != std::string::npos,
              "The debug overlay shows the game script node");
    }
    capture(engine, game, "1-message");
    testDump(engine, session.Text(), SnowRadish::kMapRoute);
    std::cout << "page: " << session.Speaker() << " / " << text->getText().substr(0, 60) << '\n';

    press(engine, SDLK_ESCAPE);
    frames(engine, 5);
    require(menu->IsOpen() && engine.isStoryPaused(), "Esc opens the pause InGameMenu");
    capture(engine, game, "2-pause");
    press(engine, SDLK_ESCAPE);
    frames(engine, 5);
    require(!menu->IsOpen(), "Esc closes the pause menu");

    press(engine, SDLK_B);
    frames(engine, 5);
    require(backlog->isVisible() && engine.isStoryPaused() && !backlog->getEntries().empty(),
            "B opens the VNBacklog with the pages read");
    capture(engine, game, "3-backlog");
    press(engine, SDLK_ESCAPE);
    frames(engine, 5);
    require(!backlog->isVisible(), "Esc closes the backlog");

    // The first map choice is ~1350 pages in: turn several pages per rendered frame.
    for (int i = 0; i < 4000 && !session.Choosing(); ++i) {
      for (int k = 0; k < 8 && !session.Choosing(); ++k) press(engine, SDLK_RETURN);
      frames(engine, 1);
    }
    require(session.Choosing(), "Timed out: first choice");
    frames(engine, 3);
    Kotonoha::ChoiceQuestionScene *choice = nullptr;
    for (auto *scene : game.GetActiveScenes())
      if (auto *c = dynamic_cast<Kotonoha::ChoiceQuestionScene *>(scene); c && c->isActive()) choice = c;
    require(choice != nullptr, "A ChoiceQuestionScene shows the script choice");
    std::cout << "choice: " << choice->getOptions().size() << " options, first '"
              << choice->getOptions().front().label << "'\n";
    const size_t before = session.Inputs().size();
    for (int i = 0; i < 100 && session.Fading(); ++i) frames(engine, 2);
    press(engine, SDLK_DOWN);
    capture(engine, game, "4-choice");
    press(engine, SDLK_RETURN);
    frames(engine, 4);
    require(session.Inputs().size() == before + 1 && session.Inputs().back() >= 0,
            "Picking an option advances the script");

    // Runtime save / load (replay of the recorded inputs).
    const auto inputs = session.Inputs();
    const auto path = saves / "test.vn";
    std::filesystem::create_directories(saves);
    game.saveGame(path.string());
    game.loadGame(path.string());
    require(game.GetState() == GameState::Gameplay && session.Playing() && session.Inputs() == inputs,
            "Load replays the story to the saved point");
    frames(engine, 10);

    // The pause menu "return to title" item goes back to the Menu state.
    press(engine, SDLK_ESCAPE);
    frames(engine, 2);
    require(menu->IsOpen(), "Esc reopens the pause menu");
    press(engine, SDLK_UP); // item 0 wraps to the last item (5)
    press(engine, SDLK_RETURN);
    frames(engine, 4);
    require(game.GetState() == GameState::Menu && !session.Playing(), "Return to title");
  }
  std::filesystem::remove_all(saves);
}

} // namespace

void testLauncher(const std::string &directory) {
  const auto dir = std::filesystem::temp_directory_path() / "snow-launcher-test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::string settings = (dir / "settings.json").string();
  {
    SnowRadish::SnowLauncher launcher(settings, (dir / "missing").string());
    require(!launcher.pathValid() && !launcher.launch(), "Launcher rejects a folder without the DATs");
    launcher.setPath(directory);
    require(launcher.pathValid(), "Launcher accepts the game folder");
    launcher.settings().upscaling.factor = 2;
    launcher.open();
    launcher.iterate();
    require(launcher.launch(), "Launcher launches with the folder");
  }
  SnowRadish::SnowLauncher reopened(settings, "");
  require(reopened.path() == directory && reopened.pathValid() &&
              reopened.settings().upscaling.factor == 2,
          "Launcher remembers the folder and the upscaling factor");
  std::filesystem::remove_all(dir);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: SnowEngineTest dat-directory\n";
    return 2;
  }
  try {
    testLauncher(argv[1]);
    testGame(argv[1]);
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Snow Radish Vacation engine tests passed\n";
  return 0;
}
