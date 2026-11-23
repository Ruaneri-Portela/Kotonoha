#include <SummerRuntime.hpp>

#include <Kotonoha/Formats/BlockPcm.hpp>
#include <Kotonoha/components/ChoiceQuestionScene.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/VNSave.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <SummerRadish/SummerAssets.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 3)
      return 77;
    auto assets = std::make_shared<Kotonoha::Providers::SummerAssets>(argv[1]);
    if (assets->name() != "MEO CMP")
      throw std::runtime_error("MEO provider identity is incorrect");
    if (argc == 3 && std::string(argv[2]) == "audio") {
      const auto bytes = assets->read("SOUND/MEO02.BGM");
      const auto bgm = assets->loadAudio("SOUND/MEO02.BGM");
      const auto decoded = Kotonoha::Formats::BlockPcm::decodeBgm(bytes);
      if (bgm.spec.format != SDL_AUDIO_S16 || bgm.spec.channels != 2 ||
          bgm.spec.freq != 44100 ||
          bgm.data.size() != (bytes.size() - 16) * 4 ||
          decoded.loopStart == 0 || decoded.loopStart >= bytes.size() - 16)
        throw std::runtime_error("MEO BGM PCM format or loop is invalid");
      bool audible = false;
      for (std::size_t i = 0; i + 2 <= bgm.data.size(); i += 2) {
        int16_t sample;
        std::memcpy(&sample, bgm.data.data() + i, sizeof(sample));
        if (sample > 1000 || sample < -1000) {
          audible = true;
          break;
        }
      }
      if (!audible)
        throw std::runtime_error("MEO BGM decoded to silence");
      const auto sndBytes = assets->read("SOUND/A-01A.SND");
      const auto snd = assets->loadAudio("SOUND/A-01A.SND");
      if (snd.spec.format != SDL_AUDIO_S16 || snd.spec.channels != 2 ||
          snd.spec.freq != 44100 || sndBytes.size() < 8 ||
          snd.data.size() != 29u * 44100u * 4u)
        throw std::runtime_error("MEO SND PCM format is invalid");
      audible = false;
      for (std::size_t i = 0; i + 2 <= snd.data.size(); i += 2) {
        int16_t sample;
        std::memcpy(&sample, snd.data.data() + i, sizeof(sample));
        if (sample > 1000 || sample < -1000) {
          audible = true;
          break;
        }
      }
      if (!audible)
        throw std::runtime_error("MEO SND decoded to silence");
      // Verify bit-exact ADPCM continuity (zero DC drift) across A-00.SND
      // blocks
      const auto a00 = assets->loadAudio("SOUND/A-00.SND");
      if (a00.data.size() < 5u * 44100u * 4u)
        throw std::runtime_error("MEO A-00.SND is truncated");
      int16_t b2EndL = 0, b3EndL = 0;
      std::memcpy(&b2EndL, a00.data.data() + (3u * 44100u - 1u) * 4u,
                  sizeof(b2EndL));
      std::memcpy(&b3EndL, a00.data.data() + (4u * 44100u - 1u) * 4u,
                  sizeof(b3EndL));
      if (b2EndL != -504 || b3EndL != -1)
        throw std::runtime_error("MEO ADPCM DC offset drift detected");
      return 0;
    }
    SummerRadish::SummerRuntime game(assets, "Miss Each Other",
                                     "me.hirameki.kotonoha.meo");
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.persistSettings = false;
    Kotonoha::Engine engine(game, options);
    struct Stop {
      SummerRadish::SummerRuntime &game;
      ~Stop() { game.shutdown(); }
    } stop{game};

    SDL_Event skipSplash{};
    skipSplash.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    skipSplash.button.button = SDL_BUTTON_LEFT;
    skipSplash.button.down = true;
    skipSplash.button.windowID = SDL_GetWindowID(&engine.GetWindow());
    engine.Event(&skipSplash);
    for (int i = 0; i < 8 && game.GetState() == Kotonoha::GameState::Splash;
         ++i)
      engine.Iterate();
    if (game.GetState() != Kotonoha::GameState::Menu ||
        game.GetActiveScenes().empty())
      throw std::runtime_error("MEO main menu did not open");
    if (game.name != "Miss Each Other" ||
        std::string(SDL_GetWindowTitle(&engine.GetWindow())) !=
            "Miss Each Other")
      throw std::runtime_error("MEO window identity is incorrect");

    auto &renderer = engine.GetRenderer();
    auto *previous = SDL_GetRenderTarget(&renderer);
    Kotonoha::TexturePtr target(SDL_CreateTexture(
        &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 960, 720));
    if (!target || !SDL_SetRenderTarget(&renderer, target.get()))
      throw std::runtime_error(SDL_GetError());
    auto saveCapture = [&](const char *name) {
      const char *dir = SDL_getenv("MEO_CAPTURE_DIR");
      if (!dir)
        return;
      std::filesystem::create_directories(dir);
      auto *prev = SDL_GetRenderTarget(&renderer);
      SDL_SetRenderTarget(&renderer, target.get());
      game.renderBackground(renderer, 1);
      game.render(renderer, 1);
      game.renderOverlay(renderer, 1);
      Kotonoha::SurfacePtr surf(SDL_RenderReadPixels(&renderer, nullptr));
      if (surf)
        SDL_SaveBMP(surf.get(),
                    (std::filesystem::path(dir) / (std::string(name) + ".bmp"))
                        .string()
                        .c_str());
      SDL_SetRenderTarget(&renderer, prev);
    };
    // Verify MEO 0verflow ROGO.TEX boot animation renders before the title
    // menu.
    SDL_Delay(1100);
    engine.Iterate();
    game.render(renderer, 1);
    saveCapture("rogo");
    Kotonoha::SurfacePtr rogo(SDL_RenderReadPixels(&renderer, nullptr));
    if (!rogo)
      throw std::runtime_error(SDL_GetError());
    const auto *rogoPixel = static_cast<const uint8_t *>(rogo->pixels) +
                            200 * rogo->pitch + 400 * 4;
    if (int(rogoPixel[0]) + rogoPixel[1] + rogoPixel[2] < 200)
      throw std::runtime_error(
          "MEO 0verflow ROGO.TEX boot logo did not render");
    // Skip ROGO.TEX to enter the MEO title menu.
    engine.Event(&skipSplash);
    engine.Iterate();
    game.render(renderer, 1);
    saveCapture("title");
    Kotonoha::SurfacePtr menu(SDL_RenderReadPixels(&renderer, nullptr));
    if (!menu)
      throw std::runtime_error(SDL_GetError());
    const auto *menuPixel = static_cast<const uint8_t *>(menu->pixels) +
                            200 * menu->pitch + 400 * 4;
    if (int(menuPixel[0]) + menuPixel[1] + menuPixel[2] < 90)
      throw std::runtime_error("MEO main menu is black");
    if (argc == 3 && std::string(argv[2]) == "scene") {
      SDL_Event navigate{};
      navigate.type = SDL_EVENT_KEY_DOWN;
      navigate.key.down = true;
      navigate.key.windowID = SDL_GetWindowID(&engine.GetWindow());
      navigate.key.key = SDLK_DOWN;
      engine.Event(&navigate);
      engine.Event(&navigate);
      navigate.key.key = SDLK_RETURN;
      engine.Event(&navigate);
      if (game.GetState() != Kotonoha::GameState::Menu)
        throw std::runtime_error("MEO Scene menu did not open");
      game.render(renderer, 1);
    }
    SDL_Event startGame{};
    if (argc == 3 && std::string(argv[2]) == "mouse") {
      startGame.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      startGame.button.button = SDL_BUTTON_LEFT;
      startGame.button.down = true;
      startGame.button.x = 500;
      startGame.button.y = 145;
      startGame.button.windowID = SDL_GetWindowID(&engine.GetWindow());
    } else {
      startGame.type = SDL_EVENT_KEY_DOWN;
      startGame.key.key = SDLK_RETURN;
      startGame.key.down = true;
      startGame.key.windowID = SDL_GetWindowID(&engine.GetWindow());
    }
    engine.Event(&startGame);
    engine.Iterate();
    if (game.GetState() != Kotonoha::GameState::Gameplay ||
        game.GetActiveScenes().empty())
      throw std::runtime_error("MEO Start did not enter gameplay");
    const auto audioRequests = assets->recentAudio();
    if (std::find(audioRequests.begin(), audioRequests.end(),
                  "SOUND/MEO02.BGM") == audioRequests.end())
      throw std::runtime_error("MEO opening BGM was not requested");
    if (std::find(audioRequests.begin(), audioRequests.end(),
                  "SOUND/A-00.SND") == audioRequests.end())
      throw std::runtime_error("MEO opening voice was not requested");
    bool visible = false;
    const Uint64 started = SDL_GetTicks();
    while (!visible && SDL_GetTicks() - started < 5000) {
      engine.Iterate();
      game.render(renderer, 1);
      Kotonoha::SurfacePtr surface(SDL_RenderReadPixels(&renderer, nullptr));
      if (!surface)
        throw std::runtime_error(SDL_GetError());
      const auto *pixels = static_cast<const uint8_t *>(surface->pixels);
      uint64_t light = 0;
      for (int y = 80; y < surface->h - 80; y += 16)
        for (int x = 80; x < surface->w - 80; x += 16) {
          const auto *pixel = pixels + y * surface->pitch + x * 4;
          light += pixel[0] + pixel[1] + pixel[2];
        }
      visible = light > 5000;
      SDL_Delay(20);
    }
    SDL_SetRenderTarget(&renderer, previous);
    if (!visible)
      throw std::runtime_error("MEO gameplay remained black");
    saveCapture("story");
    const auto savePath = std::filesystem::temp_directory_path() /
                          "kotonoha-meo-presentation-test.vn";
    struct RemoveSave {
      std::filesystem::path path;
      ~RemoveSave() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
      }
    } removeSave{savePath};
    game.saveGame(savePath.string());
    if (argc == 3 && std::string(argv[2]) == "choice") {
      auto save = Kotonoha::VNSave::read(savePath.string());
      save.gameData = "MEO-PRESENTATION-1\nA-01C.XML\n55000\n";
      save.write(savePath.string());
    } else if (argc == 3 && std::string(argv[2]) == "transition") {
      auto save = Kotonoha::VNSave::read(savePath.string());
      save.gameData = "MEO-PRESENTATION-1\nA-00.XML\n235000\n";
      save.write(savePath.string());
    }
    game.loadGame(savePath.string());
    if (game.GetState() != Kotonoha::GameState::Gameplay)
      throw std::runtime_error("MEO load did not restore gameplay");
    if (argc == 3 && std::string(argv[2]) == "choice") {
      Kotonoha::ChoiceQuestionScene *choice = nullptr;
      for (auto *scene : game.GetActiveScenes())
        if (auto *candidate =
                dynamic_cast<Kotonoha::ChoiceQuestionScene *>(scene))
          choice = candidate;
      if (!choice || !choice->isActive() || choice->getOptions().size() != 1)
        throw std::runtime_error("MEO timed choice did not open");
      saveCapture("choice");
      choice->chooseOption(0);
      if (game.debugNode().rfind("A-02.XML:", 0) != 0)
        throw std::runtime_error("MEO choice did not enter A-02.XML");
    }
    if (argc == 3 && std::string(argv[2]) == "transition" &&
        game.debugNode().rfind("A-01A.XML:", 0) != 0)
      throw std::runtime_error("MEO did not advance to A-01A.XML");
    if (argc == 3 && std::string(argv[2]) == "transition") {
      game.saveGame(savePath.string());
      auto save = Kotonoha::VNSave::read(savePath.string());
      save.gameData = "MEO-PRESENTATION-1\nA-01A.XML\n24000\n";
      save.write(savePath.string());
      game.loadGame(savePath.string());
      if (game.debugNode().rfind("A-01T.XML:", 0) != 0)
        throw std::runtime_error("MEO did not advance to A-01T.XML");
    }
    SDL_Event pause{};
    pause.type = SDL_EVENT_KEY_DOWN;
    pause.key.key = SDLK_ESCAPE;
    pause.key.down = true;
    pause.key.windowID = SDL_GetWindowID(&engine.GetWindow());
    engine.Event(&pause);
    if (!game.isMeoOverlayOpen())
      throw std::runtime_error("MEO native overlay menu did not open");
    if (Kotonoha::GetInGameMenuRuntime(engine).IsOpen())
      throw std::runtime_error(
          "MEO should open native overlay menu instead of InGameMenu");
    engine.Iterate();
    saveCapture("overlay");
    const auto pausedNode = game.debugNode();
    SDL_Delay(120);
    engine.Iterate();
    if (game.debugNode() != pausedNode)
      throw std::runtime_error("MEO timeline advanced while paused");
    engine.Event(&pause);
    if (game.isMeoOverlayOpen())
      throw std::runtime_error("MEO native overlay menu did not close");
    std::cout << "MEO rendered its opening scene\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
