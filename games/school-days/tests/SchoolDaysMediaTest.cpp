#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
Kotonoha::EngineOptions Options(const std::shared_ptr<SchoolDays::Assets> &assets) {
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "software";
  return options;
}
struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;
  Application(const std::shared_ptr<SchoolDays::Assets> &assets, const char *route, const char *documents)
      : runtime(assets, route, documents), engine(runtime, Options(assets)) {}
  ~Application() { runtime.shutdown(); }
};
}

int main(int argc, char **argv) {
  try {
    Require(argc == 4, "Usage: SchoolDaysMediaTest route fixtures documents");
    auto assets = std::make_shared<SchoolDays::Assets>(argv[2]);
    Application app(assets, argv[1], argv[3]);
    app.runtime.SetState(Kotonoha::GameState::Gameplay);
    app.engine.debugJumpToNode("0");
    for (int i = 0; i < 3; ++i)
      Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Engine media frame failed");
    bool subtitleScene = false, gameScene = false;
    for (const auto *scene : app.runtime.GetActiveScenes()) {
      subtitleScene = subtitleScene || dynamic_cast<const SchoolDays::Subtitles *>(scene) != nullptr;
      gameScene = gameScene || dynamic_cast<const SchoolDays::GameScene *>(scene) != nullptr;
    }
    Require(subtitleScene && gameScene, "School Days must register its custom presentation scenes");
    const double before = *app.runtime.debugNodeSeconds();
    SDL_Delay(40);
    Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Clock frame failed");
    Require(*app.runtime.debugNodeSeconds() > before, "Runtime node clock did not advance");
    Require(app.engine.GetDebug().nodeClockText().find("Clock:") != std::string::npos,
            "School Days node has no debug clock");
    Require(assets->cachedTextures() > 0, "ORS background was not decoded");
    for (const char *channel : {"BGM", "Voice", "Movie"})
      Require(app.engine.GetAudio().channel(channel)->activeStreams() > 0,
              "ORS audio/movie did not attach its channel stream");
    Kotonoha::SurfacePtr frame(SDL_RenderReadPixels(&app.engine.GetRenderer(), nullptr));
    Require(bool(frame), "Cannot inspect rendered movie");
    Uint8 r = 0, g = 0, b = 0, a = 0;
    Require(SDL_ReadSurfacePixel(frame.get(), frame->w * 3 / 4, frame->h * 4 / 5,
                                &r, &g, &b, &a), "Cannot inspect movie pixel");
    Require(r > 180 && g < 80 && b < 80, "WMV frame was not presented over the blue background");
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 500;
    event.button.y = 60;
    app.runtime.onEvent(event);
    const double saveClock = *app.runtime.debugNodeSeconds();
    SDL_Delay(35);
    Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Save panel frame failed");
    Require(*app.runtime.debugNodeSeconds() == saveClock, "Save panel must pause the scene");
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RIGHT;
    app.runtime.onEvent(event);
    Require(*app.runtime.debugNodeSeconds() == saveClock, "Save panel leaked gameplay input");
    event.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(event);
    Require(!app.runtime.anyPanelOpen(), "Escape did not close the save panel");
    SDL_Delay(25);
    Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Resume frame failed");
    Require(*app.runtime.debugNodeSeconds() > saveClock, "Scene did not resume after closing save panel");
    app.engine.debugJumpToNode("00/00-00-A03");
    Require(app.engine.Iterate() == SDL_APP_CONTINUE, "Choice frame failed");
    SchoolDays::GameScene *choices = nullptr;
    for (auto *scene : app.runtime.GetActiveScenes())
      if (auto *game = dynamic_cast<SchoolDays::GameScene *>(scene)) choices = game;
    Require(choices != nullptr, "Custom choice scene is missing");
    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_UP;
    Require(choices->handleEvent(event), "Custom choice keyboard navigation failed");
    event.key.key = SDLK_RETURN;
    Require(choices->handleEvent(event), "Custom choice activation failed");
    event.key.key = SDLK_DOWN;
    Require(!choices->handleEvent(event), "Committed choices are still interactive");
    std::cout << "SchoolDaysMediaTest: media, custom scenes, modal pause and choices passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysMediaTest: " << error.what() << '\n';
    return 1;
  }
}
