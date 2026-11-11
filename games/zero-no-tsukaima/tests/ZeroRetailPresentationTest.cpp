#include "ZeroRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace ZeroNoTsukaima;
static void Require(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    auto disc = std::make_shared<Kotonoha::Providers::ZeroDiscAssets>(argv[1]);
    ZeroRuntime runtime(disc);
    Kotonoha::EngineOptions options;
    options.assets = disc;
    Kotonoha::Engine engine(runtime, options);
    struct Shutdown {
      ZeroRuntime &runtime;
      ~Shutdown() { runtime.Shutdown(); }
    } shutdown{runtime};
    ZeroSession session(engine, disc);
    const bool concerto =
        session.GameEdition() == SceneSource::Edition::Concerto;
    const std::string edition = concerto ? "concerto" : "symphony";
    auto *renderer = &engine.GetRenderer();
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_TARGET, 640, 448);
    Require(target != nullptr, SDL_GetError());
    struct Target {
      SDL_Renderer *r;
      SDL_Texture *t;
      ~Target() {
        SDL_SetRenderTarget(r, nullptr);
        SDL_DestroyTexture(t);
      }
    } targetGuard{renderer, target};
    SDL_SetRenderTarget(renderer, target);
    auto capture = [&](const char *name, const std::function<void()> &draw) {
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      draw();
      Kotonoha::SurfacePtr raw{SDL_RenderReadPixels(renderer, nullptr)};
      Require(raw != nullptr, SDL_GetError());
      Kotonoha::SurfacePtr pixels{
          SDL_ConvertSurface(raw.get(), SDL_PIXELFORMAT_RGBA32)};
      Require(pixels != nullptr, SDL_GetError());
      size_t colored = 0;
      // Gameplay checks only the image area, not a text panel masking a black
      // scene.
      for (int y = 0; y < 270; ++y) {
        const auto *row =
            static_cast<const uint8_t *>(pixels->pixels) + y * pixels->pitch;
        for (int x = 0; x < 640; ++x)
          if (row[x * 4] > 20 || row[x * 4 + 1] > 20 || row[x * 4 + 2] > 20)
            ++colored;
      }
      Require(colored > 15000,
              std::string(name) + " renders a nonblank image area");
      if (const char *dir = SDL_getenv("ZERO_CAPTURE_DIR")) {
        std::filesystem::create_directories(dir);
        Require(SDL_SavePNG(pixels.get(), (std::filesystem::path(dir) /
                                           (edition + "-" + name + ".png"))
                                              .string()
                                              .c_str()),
                SDL_GetError());
      }
      std::cout << edition << " " << name << ": " << colored
                << " visible pixels\n";
    };
    Require(session.ScenePicture("MMV_LOGO") != nullptr,
            "retail boot logo resolves");
    capture("logo", [&] {
      const SDL_FRect screen{0, 0, 640, 448};
      session.ScenePicture("MMV_LOGO")->Draw(renderer, nullptr, &screen);
    });
    {
      auto movie = session.OpenMovie(0);
      Require(movie != nullptr, "retail opening movie opens");
      // PSS starts with black frames; decode to an actual image, not just an
      // archive read. Explicit media time keeps this test deterministic.
      for (int i = 0; i < 60; ++i)
        movie->update(*renderer, i / 30.0);
      capture("opening", [&] {
        const SDL_FRect screen{0, 0, 640, 448};
        movie->render(*renderer, screen, session.Upscaler(), true);
      });
    }
    Frontend frontend(session);
    frontend.Enter(false);
    SDL_Event enter{};
    enter.type = SDL_EVENT_KEY_DOWN;
    enter.key.key = SDLK_RETURN;
    enter.key.down = true;
    frontend.Event(
        enter); // Reveal the entire title without waiting for its animation.
    capture("title", [&] { frontend.Draw(renderer); });
    frontend.Event(enter);
    SDL_Delay(550); // End the real title -> menu crossfade.
    frontend.Tick();
    if (!concerto) {
      for (const char *name : {"NEW", "LOAD", "OPTI", "GALL", "SCEE", "SOUND"})
        for (const char *state : {"ON", "OFF"})
          Require(session.NormalPicture(std::string("GAMEMODE_GAME_") + name +
                                        "_" + state),
                  std::string("retail button ") + name + "_" + state);
    }
    capture("menu", [&] { frontend.Draw(renderer); });
    frontend.Leave();
    Require(session.Start(), "start retail story");
    session.DebugJump(concerto ? "0006.KS" : "ST01_04.KS");
    for (int i = 0; i < 200 && !session.WaitingForClick(); ++i)
      Require(session.Skip(), "advance presentation wait to first dialogue");
    Require(session.WaitingForClick(), "story reaches dialogue");
    Require(!session.Story()->Scene().empty(), "story has image layers");
    for (const auto &layer : session.Story()->Scene())
      std::cout << edition << " layer " << int(layer.kind) << " " << layer.image
                << " at " << layer.x << "," << layer.y
                << " alpha=" << layer.alpha << '\n';
    if (!concerto) {
      Require(session.Story()->Stage() == "ルイズの部屋 昼",
              "retail background tag executed");
      const int stage =
          disc->Source().Catalog().ResolveStage(session.Story()->Stage());
      Require(stage == disc->Source().ScenedatIndex("BGA01A"),
              "background uses the authoritative SCENEDAT table");
      Require(stage >= 0 && session.SceneIndexPicture(stage),
              "background texture resolves");
      capture("background", [&] {
        const SDL_FRect screen{0, 0, 640, 448};
        session.SceneIndexPicture(stage)->Draw(renderer, nullptr, &screen);
      });
    }
    capture("gameplay", [&] {
      session.DrawStage(renderer);
      session.DrawMessageWindow(renderer);
      session.DrawMessageText(renderer, session.Story()->Text());
    });
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
