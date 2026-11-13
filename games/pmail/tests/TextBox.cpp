#include "PmailTextBox.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Kotonoha::PmailPort;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Runtime : Kotonoha::Runtime {
  Runtime()
      : Kotonoha::Runtime("Textbox test", "1",
                          "me.hirameki.pmail.textbox-test") {}
  SDL_Point viewport() const override { return {800, 600}; }
  void initSplash(Kotonoha::Engine &) override {}
};
void checkComposition(PmailTextBox &box,
                      Kotonoha::Providers::SnowAssets &assets,
                      Kotonoha::Engine &engine) {
  auto &renderer = engine.GetRenderer();
  auto *target = SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_TARGET, 800, 600);
  require(target && SDL_SetRenderTarget(&renderer, target),
          "Cannot create render target");
  auto expected =
      Kotonoha::SurfacePtr(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32));
  require(bool(expected), "Cannot create reference canvas");
  // A light stage makes a missing backing visible and exercises CHAR alpha.
  SDL_FillSurfaceRect(expected.get(), nullptr,
                      SDL_MapSurfaceRGBA(expected.get(), 190, 210, 230, 255));
  for (const auto *name : {"wakuh", "waku", "bot"}) {
    auto source =
        assets.loadSurfaceAsset("graphic/" + std::string(name) + ".CHAR");
    require(bool(source), "Missing original message-window layer");
    if (std::string(name) == "wakuh") {
      source.reset(SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32));
      require(bool(source), "Cannot convert backing");
      for (int y = 0; y < source->h; ++y) {
        auto *row = static_cast<uint8_t *>(source->pixels) + y * source->pitch;
        for (int x = 0; x < source->w; ++x) {
          // Original locpu.dll AddRgbChr uses 8-bit fixed point, not /255.
          const int blue = row[x * 4 + 2];
          row[x * 4 + 2] = uint8_t(blue + (255 - blue) * 150 / 256);
        }
      }
    }
    const auto placement = assets.Sprite(name);
    require(placement.valid, "Missing native layer coordinates");
    SDL_Rect destination{int(placement.x), int(placement.y), source->w,
                         source->h};
    SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_BLEND);
    require(
        SDL_BlitSurface(source.get(), nullptr, expected.get(), &destination),
        "Cannot compose native reference");
  }
  SDL_SetRenderDrawColor(&renderer, 190, 210, 230, 255);
  SDL_RenderClear(&renderer);
  box.render(renderer, 1);
  auto captured =
      Kotonoha::SurfacePtr(SDL_RenderReadPixels(&renderer, nullptr));
  require(bool(captured), "Cannot capture textbox");
  auto actual = Kotonoha::SurfacePtr(
      SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32));
  require(actual && actual->w == 800 && actual->h == 600,
          "Unexpected render dimensions");
  bool backingChangesStage = false;
  for (int y = 0; y < 600; ++y) {
    const auto *a =
        static_cast<const uint8_t *>(actual->pixels) + y * actual->pitch;
    const auto *e =
        static_cast<const uint8_t *>(expected->pixels) + y * expected->pitch;
    for (int x = 0; x < 800; ++x) {
      for (int c = 0; c < 3; ++c)
        if (std::abs(int(a[x * 4 + c]) - int(e[x * 4 + c])) > 1)
          throw std::runtime_error("Textbox composition differs at " +
                                   std::to_string(x) + "," + std::to_string(y));
      if (x > 100 && x < 650 && y > 500 && y < 550 && e[x * 4] < 150)
        backingChangesStage = true;
    }
  }
  require(backingChangesStage,
          "Backing remains transparent in the dialogue region");
  SDL_SaveBMP(actual.get(),
              (std::filesystem::temp_directory_path() / "pmail-textbox.bmp")
                  .string()
                  .c_str());
  SDL_SetRenderTarget(&renderer, nullptr);
  SDL_DestroyTexture(target);
}
void checkAuto(PmailTextBox &box, PmailGame &game) {
  auto object = std::make_shared<Pmail::ScriptObject>();
  object->id = 0xffe;
  object->strings = {"Auto timing"};
  game.vm().registerObject(object);
  const Pmail::Arg line{(0xffeu << 20) | (2u << 16)};
  game.native(game.vm(), 4, {line});
  require(game.waitingForClick(), "Dialogue native did not block");
  static_cast<Kotonoha::VNTextBox &>(box).setDialogue("", game.message());
  box.completeDialogue();
  box.setAutoMode(true);
  bool voicePlaying = true;
  game.voicePlaying = [&] { return voicePlaying; };
  box.update();
  SDL_Delay(2050);
  box.update();
  require(game.waitingForClick(), "Auto interrupted the voice");
  voicePlaying = false;
  box.update();
  require(game.waitingForClick(), "Auto skipped its post-voice delay");
  box.setPresentationPaused(true);
  SDL_Delay(2050);
  box.update();
  require(game.waitingForClick(), "Auto advanced behind a menu");
  box.setPresentationPaused(false);
  box.update();
  require(game.waitingForClick(), "Menu time counted towards auto delay");
  SDL_Event hide{};
  hide.type = SDL_EVENT_KEY_DOWN;
  hide.key.key = SDLK_H;
  require(box.onEvent(hide) && !box.isVisible(), "H did not hide the textbox");
  box.show(); // Components calls show every frame for a nonempty message.
  require(!box.isVisible(), "Scene show overrode user-hidden state");
  SDL_Delay(2050);
  box.update();
  require(game.waitingForClick(), "Auto advanced while textbox was hidden");
  SDL_Event restore{};
  restore.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  restore.button.button = SDL_BUTTON_LEFT;
  require(box.onEvent(restore) && box.isVisible(),
          "Click did not restore textbox");
  require(game.waitingForClick(), "Restore click also advanced dialogue");
  box.update();
  require(game.waitingForClick(), "Hidden time counted towards auto delay");
  SDL_Delay(2050);
  box.update();
  require(!game.waitingForClick(),
          "Auto did not advance after voice-end plus 2000ms");
  require(box.autoMode(), "Auto did not remain enabled across dialogue");
  game.voicePlaying = {};
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "Usage: PmailTextBoxCheck PMAILDVD");
    auto assets = std::make_shared<Kotonoha::Providers::SnowAssets>(argv[1]);
    const auto path = std::filesystem::path(argv[1]) / "data/pack/script.dat";
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    PmailGame game(std::move(bytes), path.string());
    require(game.init(), "Game init failed");
    Runtime runtime;
    Kotonoha::EngineOptions options;
    options.assets = assets;
    options.launcher = false;
    options.persistSettings = false;
    Kotonoha::Engine engine(runtime, options);
    PmailTextBox box(engine, game, *assets);
    checkComposition(box, *assets, engine);
    checkAuto(box, game);
    std::cout << "Original window layers/tint, voice-end auto delay and "
                 "hidden/menu suspension passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
