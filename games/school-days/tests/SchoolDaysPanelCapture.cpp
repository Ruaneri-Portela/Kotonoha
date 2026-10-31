// Renders the native School Days screens with retail data and saves BMP
// captures for visual comparison with the original layout. Not a ctest: it
// needs user-supplied game data.
#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
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
  Application(const std::shared_ptr<SchoolDays::Assets> &assets, const std::string &route,
              const std::string &documents)
      : runtime(assets, route, documents), engine(runtime, Options(assets)) {}
  ~Application() { runtime.shutdown(); }
};

void Frame(Application &app, int count = 1) {
  for (int i = 0; i < count; ++i) {
    if (app.engine.Iterate() != SDL_APP_CONTINUE) throw std::runtime_error("Frame failed");
    SDL_Delay(16);
  }
}

void Capture(Application &app, const std::filesystem::path &file) {
  Kotonoha::SurfacePtr frame(SDL_RenderReadPixels(&app.engine.GetRenderer(), nullptr));
  if (!frame || !SDL_SaveBMP(frame.get(), file.string().c_str())) throw std::runtime_error(SDL_GetError());
  std::cout << file.string() << '\n';
}

void Pointer(Application &app, float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.x = x;
  event.motion.y = y;
  app.runtime.onEvent(event);
}
}

int main(int argc, char **argv) {
  try {
    if (argc != 6)
      throw std::runtime_error("Usage: SchoolDaysPanelCapture route data scripts documents output");
    const std::filesystem::path output = argv[5];
    std::filesystem::create_directories(output);
    auto assets = std::make_shared<SchoolDays::Assets>(argv[2], argv[3]);
    Application app(assets, argv[1], argv[4]);
    Frame(app, 30);
    Capture(app, output / "01-engine-splash.bmp");
    for (int i = 0; i < 900 && app.runtime.GetState() == Kotonoha::GameState::Splash; ++i) {
      Frame(app);
      if (i % 60 == 0) Capture(app, output / ("02-splash-" + std::to_string(i) + ".bmp"));
    }
    if (app.runtime.GetState() != Kotonoha::GameState::Menu) throw std::runtime_error("Splash did not finish");
    Frame(app, 5);
    Capture(app, output / "03-title.bmp");
    const std::pair<int, const char *> menuPanels[]{
        {13, "04-config.bmp"}, {11, "05-load.bmp"}, {15, "06-route-map.bmp"}, {16, "07-exit.bmp"}};
    for (const auto &[control, name] : menuPanels) {
      app.runtime.openPanel(control);
      Pointer(app, 640, 360);
      Frame(app, 3);
      Capture(app, output / name);
    }
    app.runtime.openPanel(16);
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(escape);
    app.runtime.SetState(Kotonoha::GameState::Gameplay);
    app.engine.debugJumpToNode("0");
    Frame(app, 240);
    Capture(app, output / "08-gameplay.bmp");
    const std::pair<int, const char *> gamePanels[]{{10, "09-save.bmp"}, {12, "10-backlog.bmp"}};
    for (const auto &[control, name] : gamePanels) {
      app.runtime.openPanel(control);
      Frame(app, 3);
      Capture(app, output / name);
      app.runtime.onEvent(escape);
      Frame(app);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
