// Zero no Tsukaima on the Kotonoha engine, against the user's disc image:
// the disc AssetProvider and the polymorphic components through their engine
// base classes (title -> story -> menus -> choice -> save / load).
#include "DiscAssetProvider.hpp"
#include "ZeroLauncher.hpp"
#include "ZeroRuntime.hpp"

#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/media/MediaPlayer.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
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

void frames(Kotonoha::Engine &engine, int count) {
  for (int i = 0; i < count; ++i) {
    engine.Iterate();
    SDL_Delay(5);
  }
}

// Presses `key` until `done` holds; fails after `limit` presses.
void pressUntil(Kotonoha::Engine &engine, SDL_Keycode key, const std::function<bool()> &done, int limit,
                const char *what) {
  for (int i = 0; i < limit && !done(); ++i) {
    press(engine, key);
    frames(engine, 1);
  }
  require(done(), std::string("Timed out: ") + what);
}

// ZERO_CAPTURE_DIR=dir: saves the novel space (the game viewport) of the current frame as BMP.
void capture(Kotonoha::Engine &engine, Kotonoha::Runtime &game, const char *name) {
  const char *dir = SDL_getenv("ZERO_CAPTURE_DIR");
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

void testProvider(ZeroNoTsukaima::DiscAssetProvider &disc) {
  require(disc.name().find("zero") != std::string::npos || !disc.name().empty(), "Provider name");
  require(disc.contains("scenedat/0"), "scenedat/0 is on the disc");
  require(!disc.read("scenedat/0").empty(), "scenedat/0 reads");
  require(disc.contains("normal/MAIN_MESSAGEBASE"), "Message window picture is on the disc");
  require(!disc.contains("normal/NOT_A_PICTURE"), "Unknown pictures are rejected");
  auto surface = disc.loadSurface("normal/MAIN_MESSAGEBASE");
  require(surface && surface->w > 0 && surface->h > 0, "TIM2 decodes to a surface");
  require(!disc.list("normal/").empty(), "Provider lists the normal pictures");
}

// The opening PSS through the engine MediaPlayer (FFmpeg video) with the
// demuxed SShd/SSbd PCM as its WAV soundtrack.
void testMovie(Kotonoha::Engine &engine, ZeroNoTsukaima::DiscAssetProvider &disc) {
  require(disc.contains("movie/0") && disc.contains("movie/0.wav"), "Opening movie is on the disc");
  const Kotonoha::AudioClip pcm = disc.loadAudio("movie/0.wav");
  require(pcm.spec.freq > 0 && pcm.spec.channels > 0 && !pcm.data.empty(), "Movie PCM decodes as WAV");
  Kotonoha::MediaPlayer movie(engine.GetAudio());
  movie.open(disc, "movie/0", false, "bgm");
  movie.openSoundtrack(disc, "movie/0.wav");
  require(movie.isOpen() && !movie.finished() && movie.duration() > 1, "MediaPlayer opens the PSS");
  for (int i = 0; i < 60 && !movie.hasVideoFrame(); ++i) movie.update(engine.GetRenderer(), i / 30.0);
  require(movie.hasVideoFrame(), "MediaPlayer decodes a PSS frame");
  movie.stop();
  require(movie.finished(), "Stopped movie is finished");

  // Plays the shortest movie to the end on the self-timed clock.
  const auto &names = disc.Source().Movies();
  size_t shortest = 0;
  auto size = [&](size_t i) -> Uint64 {
    const auto *entry = disc.getDisc().find("MOVIE/" + names[i]);
    return entry ? entry->size : ~Uint64(0);
  };
  for (size_t i = 1; i < names.size(); ++i)
    if (size(i) < size(shortest)) shortest = i;
  const std::string id = "movie/" + std::to_string(shortest);
  movie.open(disc, id, false, "bgm");
  movie.openSoundtrack(disc, id + ".wav");
  const double length = movie.duration();
  const Uint64 start = SDL_GetTicks();
  while (!movie.finished() && SDL_GetTicks() - start < Uint64((length + 5) * 1000)) {
    movie.update(engine.GetRenderer());
    SDL_Delay(5);
  }
  require(movie.finished(), "A PSS movie finishes with its soundtrack");
  std::cout << names[shortest] << ": " << length << " s, finished after "
            << (SDL_GetTicks() - start) / 1000.0 << " s\n";
}

void testGame(const std::string &discPath) {
  auto disc = std::make_shared<ZeroNoTsukaima::DiscAssetProvider>(discPath);
  testProvider(*disc);

  ZeroNoTsukaima::ZeroRuntime game(disc);
  {
    Kotonoha::EngineOptions options;
    options.assets = disc;
    Kotonoha::Engine engine(game, options);
    require(&engine.GetAssets() == disc.get(), "The disc is the engine AssetProvider");
    skipSplash(engine, game);
    testMovie(engine, *disc);
    require(game.GetState() == GameState::Menu, "Starts at the retail front end");

    auto *title = find<Kotonoha::GameMenu>(game);
    require(title && title->isVisible(), "Title is a visible GameMenu");
    frames(engine, 10);
    capture(engine, game, "0-boot");

    // Logos -> title -> mode menu -> New Game, all through GameMenu::activate.
    pressUntil(engine, SDLK_RETURN, [&] { return game.GetState() == GameState::Gameplay; }, 400, "new game");
    ZeroNoTsukaima::ZeroSession &session = *game.Session();
    require(session.Playing(), "Story is playing");
    std::cout << "story started\n";

    auto *text = find<Kotonoha::VNTextBox>(game);
    auto *menu = find<Kotonoha::InGameMenu>(game);
    auto *backlog = find<Kotonoha::VNBacklog>(game);
    int choiceScenes = 0;
    for (auto *scene : game.GetActiveScenes())
      if (dynamic_cast<Kotonoha::ChoiceQuestionScene *>(scene)) ++choiceScenes;
    require(text && menu && backlog && choiceScenes == 2, "Gameplay uses the engine components");

    pressUntil(engine, SDLK_RETURN, [&] { return session.HasMessage(); }, 400, "first message");
    frames(engine, 2);
    require(text->isVisible() && text->getText() == session.Story()->Text(), "VNTextBox follows the script page");
    {
      const auto node = engine.GetDebug().nodeClockText();
      std::cout << node << '\n';
      require(node.rfind("Node: ", 0) == 0 && node.find(".KS") != std::string::npos,
              "The debug overlay shows the game script node");
    }
    capture(engine, game, "1-message");
    testDump(engine, session.Story()->Text(), int(session.Story()->SceneIndex()));
    {
      // Pictures go through the engine upscaler: 2x texture, same 640x448 size.
      ZeroNoTsukaima::Picture *plain = session.NormalPicture("MAIN_MESSAGEBASE");
      require(plain, "Message window picture");
      const float w = plain->Width(), h = plain->Height();
      Kotonoha::EngineSettings upscaled = engine.GetSettings();
      const Kotonoha::EngineSettings original = upscaled;
      upscaled.upscaling.backend = Kotonoha::UpscaleBackend::Linear;
      upscaled.upscaling.factor = 2;
      engine.applySettings(upscaled, false);
      ZeroNoTsukaima::Picture *big = session.NormalPicture("MAIN_MESSAGEBASE");
      float tw = 0, th = 0;
      require(big && SDL_GetTextureSize(big->Source(), &tw, &th), "Upscaled message window");
      require(tw == 2 * w && th == 2 * h && big->Width() == w && big->Height() == h,
              "Upscaled pictures keep their game size with 2x texels");
      frames(engine, 2);
      capture(engine, game, "1-message-upscaled");
      engine.applySettings(original, false);
      require(session.NormalPicture("MAIN_MESSAGEBASE")->Width() == w, "Upscaling off restores the pictures");
    }
    std::cout << "page: " << session.Story()->Speaker() << " / " << text->getText().substr(0, 60) << '\n';

    // Esc: CommandState as the InGameMenu (pauses the story), Esc again closes it.
    press(engine, SDLK_ESCAPE);
    frames(engine, 30);
    require(menu->IsOpen() && engine.isStoryPaused(), "Esc opens CommandState through InGameMenu");
    capture(engine, game, "2-command");
    pressUntil(engine, SDLK_ESCAPE, [&] { return !menu->IsOpen(); }, 20, "close command menu");
    frames(engine, 30);

    // B: HistoryState as the VNBacklog.
    press(engine, SDLK_B);
    frames(engine, 30);
    require(backlog->isVisible() && engine.isStoryPaused(), "B opens HistoryState through VNBacklog");
    capture(engine, game, "3-history");
    pressUntil(engine, SDLK_ESCAPE, [&] { return !backlog->isVisible(); }, 20, "close backlog");
    frames(engine, 30);

    // Click through to the first choice and pick it through ChoiceQuestionScene.
    pressUntil(engine, SDLK_RETURN, [&] { return session.Choosing(); }, 3000, "first choice");
    frames(engine, 2);
    if (!session.RouletteActive()) {
      Kotonoha::ChoiceQuestionScene *choice = nullptr;
      for (auto *scene : game.GetActiveScenes())
        if (auto *c = dynamic_cast<Kotonoha::ChoiceQuestionScene *>(scene); c && c->isActive()) choice = c;
      require(choice != nullptr, "A ChoiceQuestionScene shows the script choice");
      std::cout << "choice: " << choice->getOptions().size() << " options, first '"
                << choice->getOptions().front().label << "'\n";
      press(engine, SDLK_DOWN);
      capture(engine, game, "4-choice");
      press(engine, SDLK_RETURN);
      frames(engine, 4);
      require(!session.Choosing() || !choice->isActive(), "Picking an option advances the script");
    }

    // Runtime save / load (replay of the recorded inputs).
    const auto path = std::filesystem::temp_directory_path() / ("zero-" + std::to_string(SDL_GetTicksNS()) + ".sav");
    game.saveGame(path.string());
    game.loadGame(path.string());
    std::filesystem::remove(path);
    require(game.GetState() == GameState::Gameplay && session.Playing(), "Load resumes the story");
    frames(engine, 10);

    game.Shutdown();
  }
}

} // namespace

void testLauncher(const std::string &discPath) {
  const auto dir = std::filesystem::temp_directory_path() / "zero-launcher-test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::string settings = (dir / "settings.json").string();
  {
    ZeroNoTsukaima::ZeroLauncher launcher(settings, (dir / "missing.bin").string());
    require(!launcher.pathValid() && !launcher.launch(), "Launcher rejects a missing disc");
    launcher.setPath(discPath);
    require(launcher.pathValid(), "Launcher accepts the game disc");
    launcher.settings().upscaling.factor = 2;
    launcher.open();
    launcher.iterate();
    require(launcher.launch(), "Launcher launches with the disc");
  }
  ZeroNoTsukaima::ZeroLauncher reopened(settings, "");
  require(reopened.path() == discPath && reopened.pathValid() &&
              reopened.settings().upscaling.factor == 2,
          "Launcher remembers the disc and the upscaling factor");
  std::filesystem::remove_all(dir);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: ZeroEngineTest disc.bin\n";
    return 2;
  }
  try {
    testLauncher(argv[1]);
    testGame(argv[1]);
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Zero no Tsukaima engine tests passed\n";
  return 0;
}
