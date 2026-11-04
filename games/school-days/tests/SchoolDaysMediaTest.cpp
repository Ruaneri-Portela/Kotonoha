#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
Kotonoha::EngineOptions
Options(const std::shared_ptr<SchoolDays::Assets> &assets) {
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "software";
  return options;
}
struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;
  Application(const std::shared_ptr<SchoolDays::Assets> &assets,
              const char *route, const char *documents)
      : runtime(assets, route, documents), engine(runtime, Options(assets)) {}
  ~Application() { runtime.shutdown(); }
};

void RetinaSubtitles(Kotonoha::Engine &engine) {
  auto &renderer = engine.GetRenderer();
  Kotonoha::TexturePtr target(SDL_CreateTexture(
      &renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 2560, 1440));
  Require(bool(target) && SDL_SetRenderTarget(&renderer, target.get()),
          "Create School Days Retina target");
  SchoolDays::Subtitles subtitles(engine);
  subtitles.setCues({{0, 5, "School Days Retina", ""}});
  const auto capture = [&] {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    subtitles.render(renderer, 1);
    return Kotonoha::SurfacePtr(SDL_RenderReadPixels(&renderer, nullptr));
  };
  Require(SDL_SetRenderLogicalPresentation(&renderer, 0, 0,
                                           SDL_LOGICAL_PRESENTATION_DISABLED) &&
              SDL_SetRenderScale(&renderer, 2, 2),
          "Configure School Days physical-pixel reference");
  const auto physical = capture();
  auto &upscaler = engine.GetUpscaler();
  const auto previousUpscaling = upscaler.getSettings();
  upscaler.configure({Kotonoha::UpscaleBackend::Linear,
                      Kotonoha::UpscalePreset::Anime4K,
                      4,
                      {}});
  Require(SDL_SetRenderScale(&renderer, 1, 1) &&
              SDL_SetRenderLogicalPresentation(
                  &renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX),
          "Configure School Days Retina logical presentation");
  const auto retina = capture();
  Require(physical && retina && physical->w == retina->w &&
              physical->h == retina->h && physical->format == retina->format,
          "Read School Days Retina frame");
  bool visible = false;
  for (int y = 0; y < physical->h; ++y) {
    const auto *row =
        static_cast<const Uint8 *>(physical->pixels) + y * physical->pitch;
    Require(
        std::memcmp(
            row, static_cast<const Uint8 *>(retina->pixels) + y * retina->pitch,
            physical->w * SDL_BYTESPERPIXEL(physical->format)) == 0,
        "School Days Retina subtitles must match native pixel rasterization");
    for (int x = 0; x < physical->w; ++x) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      Require(SDL_ReadSurfacePixel(physical.get(), x, y, &r, &g, &b, &a),
              "Read School Days subtitle pixel");
      visible = visible || r > 20 || g > 20 || b > 20;
    }
  }
  Require(visible, "School Days Retina reference must contain rendered glyphs");
  upscaler.configure(previousUpscaling);
  Require(SDL_SetRenderTarget(&renderer, nullptr),
          "Restore School Days window target");
}
} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 4, "Usage: SchoolDaysMediaTest route fixtures documents");
    Require(SchoolDays::Subtitles::dialogueText("A\\nB\\NC\\r\\nD\\tE") ==
                "A\nB\nC\nD E",
            "ORS line escapes must become actual line breaks");
    Require(SchoolDays::Subtitles::dialogueText("A\r\nB") == "A\nB",
            "CRLF must produce one subtitle line break");
    Require(SchoolDays::Subtitles::dialogueText("{\\pos(1,2)}") ==
                "{\\pos(1,2)}",
            "ASS-like text stays plain text for the shared serializer");
    auto assets = std::make_shared<SchoolDays::Assets>(argv[2]);
    Application app(assets, argv[1], argv[3]);
    RetinaSubtitles(app.engine);
    app.runtime.SetState(Kotonoha::GameState::Gameplay);
    app.engine.debugJumpToNode("0");
    for (int i = 0; i < 3; ++i)
      Require(app.engine.Iterate() == SDL_APP_CONTINUE,
              "Engine media frame failed");
    bool subtitleScene = false, gameScene = false;
    for (const auto *scene : app.runtime.GetActiveScenes()) {
      subtitleScene =
          subtitleScene ||
          dynamic_cast<const SchoolDays::Subtitles *>(scene) != nullptr;
      gameScene = gameScene ||
                  dynamic_cast<const SchoolDays::GameScene *>(scene) != nullptr;
    }
    Require(subtitleScene && gameScene,
            "School Days must register its custom presentation scenes");
    const double before = *app.runtime.debugNodeSeconds();
    SDL_Delay(40);
    Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Clock frame failed");
    Require(*app.runtime.debugNodeSeconds() > before,
            "Runtime node clock did not advance");
    Require(app.engine.GetDebug().nodeClockText().find("Clock:") !=
                std::string::npos,
            "School Days node has no debug clock");
    Require(assets->cachedTextures() > 0, "ORS background was not decoded");
    for (const char *channel : {"BGM", "Voice", "Movie"})
      Require(app.engine.GetAudio().channel(channel)->activeStreams() > 0,
              "ORS audio/movie did not attach its channel stream");
    Kotonoha::SurfacePtr frame(
        SDL_RenderReadPixels(&app.engine.GetRenderer(), nullptr));
    Require(bool(frame), "Cannot inspect rendered movie");
    Uint8 r = 0, g = 0, b = 0, a = 0;
    Require(SDL_ReadSurfacePixel(frame.get(), frame->w * 3 / 4,
                                 frame->h * 4 / 5, &r, &g, &b, &a),
            "Cannot inspect movie pixel");
    Require(r > 180 && g < 80 && b < 80,
            "WMV frame was not presented over the blue background");
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 500;
    event.button.y = 60;
    app.runtime.onEvent(event);
    const double saveClock = *app.runtime.debugNodeSeconds();
    SDL_Delay(35);
    Require(app.engine.Iterate() == SDL_APP_CONTINUE,
            "Save panel frame failed");
    Require(*app.runtime.debugNodeSeconds() == saveClock,
            "Save panel must pause the scene");
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RIGHT;
    app.runtime.onEvent(event);
    Require(*app.runtime.debugNodeSeconds() == saveClock,
            "Save panel leaked gameplay input");
    event.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(event);
    Require(!app.runtime.anyPanelOpen(), "Escape did not close the save panel");
    SDL_Delay(25);
    Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Resume frame failed");
    Require(*app.runtime.debugNodeSeconds() > saveClock,
            "Scene did not resume after closing save panel");
    // The fallback save UI must ask for a name even without a suggestion.
    const auto savePath =
        std::filesystem::path(argv[3]) / "Saves/Regression Save.vn";
    std::filesystem::remove(savePath);
    auto saveNameDialog = [&]() -> Kotonoha::TextInputDialog * {
      for (auto *scene : app.runtime.GetActiveScenes())
        if (auto *dialog = dynamic_cast<Kotonoha::TextInputDialog *>(scene))
          return dialog;
      return nullptr;
    };
    app.runtime.openPanel(10);
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RETURN;
    app.runtime.onEvent(event);
    Require(saveNameDialog() != nullptr, "New save must ask for a name");
    app.engine.Iterate();
    event.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(event);
    Require(!saveNameDialog() && !std::filesystem::exists(savePath),
            "Cancelling the name dialog must not create a save");
    event.key.key = SDLK_RETURN;
    app.runtime.onEvent(event);
    Require(saveNameDialog() != nullptr, "Name prompt can be reopened");
    saveNameDialog()->submitText("Regression Save");
    Require(std::filesystem::is_regular_file(savePath) && !saveNameDialog(),
            "Named save must close its dialog without invalidating the scene");
    const auto savedNode = app.runtime.debugNode();
    app.runtime.loadGame(savePath.string());
    Require(app.runtime.debugNode() == savedNode,
            "Named save reloads the same scene");
    // Auto is disabled by default: Right must commit the scene transition.
    app.engine.debugJumpToNode("0");
    const auto beforeSkip = app.runtime.debugNode();
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_SPACE;
    app.runtime.onEvent(event); // Exercise Skip while paused as well.
    event.key.key = SDLK_RIGHT;
    app.runtime.onEvent(event);
    Require(app.runtime.debugNode() != beforeSkip,
            "Skip parked at Next instead of advancing with Auto disabled");
    app.engine.debugJumpToNode("0");
    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 350;
    event.button.y = 60; // Fallback bar's Skip button.
    app.runtime.onEvent(event);
    Require(app.runtime.debugNode() != beforeSkip,
            "The visible Skip button must use the same transition as Right");
    app.engine.debugJumpToNode("00/00-00-A03");
    Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Choice frame failed");
    SchoolDays::GameScene *choices = nullptr;
    for (auto *scene : app.runtime.GetActiveScenes())
      if (auto *game = dynamic_cast<SchoolDays::GameScene *>(scene))
        choices = game;
    Require(choices != nullptr, "Custom choice scene is missing");
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_UP;
    Require(choices->handleEvent(event),
            "Custom choice keyboard navigation failed");
    const auto choiceNode = app.runtime.debugNode();
    event.key.key = SDLK_RIGHT;
    app.runtime.onEvent(event);
    Require(app.runtime.debugNode() == choiceNode,
            "Skip must not bypass an active choice");
    event.key.key = SDLK_RETURN;
    Require(choices->handleEvent(event), "Custom choice activation failed");
    event.key.key = SDLK_DOWN;
    Require(!choices->handleEvent(event),
            "Committed choices are still interactive");
    std::cout << "SchoolDaysMediaTest: media, custom scenes, modal pause and "
                 "choices passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysMediaTest: " << error.what() << '\n';
    return 1;
  }
}
