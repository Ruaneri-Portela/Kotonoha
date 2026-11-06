#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;
  static Kotonoha::EngineOptions
  options(const std::shared_ptr<SchoolDays::Assets> &assets) {
    Kotonoha::EngineOptions result;
    result.assets = assets;
    result.persistSettings = false;
    result.rendererOverride = "software";
    return result;
  }
  Application(const std::shared_ptr<SchoolDays::Assets> &assets,
              const char *route, const std::filesystem::path &documents)
      : runtime(assets, route, documents), engine(runtime, options(assets)) {}
  ~Application() { runtime.shutdown(); }
};

void checkFontLifetimeAcrossEngineConstructorException(
    const std::shared_ptr<SchoolDays::Assets> &assets, const char *route,
    const std::filesystem::path &documents) {
  // Engine creates two UI-owned TTF sessions, then invokes Runtime::init,
  // which creates GameScene's choice font. A constructor exception *after*
  // Runtime::init destroys those UI sessions before Runtime's destructor.
  // This must not close the shared FreeType library while the choice font
  // remains alive. No retail saves or assets are modified by this test.
  const auto failingDocuments = documents / "ctor-unwind";
  SchoolDays::Runtime failingRuntime(assets, route, failingDocuments);
  auto settings = Application::options(assets);
  // After Runtime::init, translation dump to an existing directory throws.
  // Either an empty source set or an invalid output file ensures failure.
  settings.dumpTranslationPath = failingDocuments.string();
  bool threw = false;
  try {
    Kotonoha::Engine failingEngine(failingRuntime, settings);
  } catch (const std::exception &) {
    threw = true;
  }
  check(threw, "Expected an Engine constructor exception after Runtime::init");
  // failingRuntime leaves this scope *after* Engine UI's partial destruction.
}

void clickChoice(SchoolDays::Runtime &runtime, int choice) {
  const auto &view = runtime.panels().ExitView();
  const int width = static_cast<int>(view.Cmap().Width());
  const int height = static_cast<int>(view.Cmap().Height());
  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 2) {
      if (view.HitChoice(x, y) != choice)
        continue;
      const float displayX = static_cast<float>(x) * 1280.0f / width;
      const float displayY = static_cast<float>(y) * 720.0f / height;
      SDL_Event event{};
      event.type = SDL_EVENT_MOUSE_MOTION;
      event.motion.x = displayX;
      event.motion.y = displayY;
      runtime.onEvent(event);
      event = {};
      event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      event.button.button = SDL_BUTTON_LEFT;
      event.button.x = displayX;
      event.button.y = displayY;
      runtime.onEvent(event);
      return;
    }
  }
  throw std::runtime_error("Retail ExitView choice was not found");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 3, "Expected retail assets and route");
    const auto documents =
        std::filesystem::temp_directory_path() /
        ("school-days-exit-" + std::to_string(SDL_GetPerformanceCounter()));
    auto assets = std::make_shared<SchoolDays::Assets>(argv[1]);
    std::cerr << "ExitRuntime: verify constructor exception font lifecycle\n";
    checkFontLifetimeAcrossEngineConstructorException(assets, argv[2], documents);
    std::cerr << "ExitRuntime: initialize retail data\n";
    Application app(assets, argv[2], documents);
    std::cerr << "ExitRuntime: enter gameplay\n";
    app.runtime.SetState(Kotonoha::GameState::Gameplay);
    app.runtime.debugJumpToNode("0");

    std::cerr << "ExitRuntime: open quit popup\n";
    check(app.runtime.handleNativeQuitRequest(),
          "WM_CLOSE must be consumed by the retail quit confirmation");
    check(app.runtime.handleNativeQuitRequest(),
          "duplicate WM_CLOSE must not force shutdown during the exit fade");
    check(!app.runtime.panels().IsExitOpen(),
          "Quit popup appeared before the entry transition");
    SDL_Delay(520);
    app.runtime.update();
    check(app.runtime.panels().IsExitOpen(),
          "Quit popup did not open after the entry transition");
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(escape);
    check(!app.runtime.panels().IsExitOpen(),
          "Escape did not close the quit popup");
    SDL_Delay(520);
    app.runtime.update();
    check(app.runtime.GetState() == Kotonoha::GameState::Gameplay,
          "Cancel changed gameplay state");

    std::cerr << "ExitRuntime: open title popup\n";
    app.runtime.openPanel(14);
    check(!app.runtime.panels().IsExitOpen(),
          "Title popup appeared before the entry transition");
    SDL_Delay(520);
    app.runtime.update();
    check(app.runtime.panels().IsExitOpen(),
          "Title popup did not open after the entry transition");
    std::cerr << "ExitRuntime: confirm title\n";
    clickChoice(app.runtime, 0);
    check(app.runtime.GetState() == Kotonoha::GameState::Gameplay,
          "Title confirmation skipped the exit animation");
    SDL_Delay(520);
    app.runtime.update();
    check(app.runtime.GetState() == Kotonoha::GameState::Menu,
          "Title confirmation did not return to the menu");
    app.runtime.openPanel(16);
    check(!app.runtime.panels().IsExitOpen(),
          "Menu quit popup appeared before the entry transition");
    SDL_Delay(520);
    app.runtime.update();
    check(app.runtime.panels().IsExitOpen(),
          "Menu quit popup did not open after the entry transition");
    app.runtime.onEvent(escape);
    check(!app.runtime.panels().IsExitOpen(),
          "Escape did not close the menu quit popup");
    SDL_Delay(520);
    app.runtime.update();
    app.runtime.openPanel(16);
    SDL_Delay(520);
    app.runtime.update();
    check(app.runtime.panels().IsExitOpen(), "Menu quit popup did not reopen");
    clickChoice(app.runtime, 0);
    SDL_Delay(770);
    app.runtime.update();
    bool sawQuit = false;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
      sawQuit |= event.type == SDL_EVENT_QUIT;
    check(sawQuit, "Menu quit confirmation did not request shutdown");
    check(!app.runtime.handleNativeQuitRequest(),
          "confirmed YES must allow the native SDL_QUIT to terminate the app");
    std::cout << "Retail exit and title transition runtime passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
