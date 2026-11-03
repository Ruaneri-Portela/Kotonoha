#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const std::string &message) {
  if (!value)
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
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "Expected retail data and route");
    struct Directory {
      std::filesystem::path path =
          std::filesystem::temp_directory_path() /
          ("school-days-save-" + std::to_string(SDL_GetPerformanceCounter()));
      ~Directory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
      }
    } documents;
    auto assets = std::make_shared<SchoolDays::Assets>(argv[1]);
    Application app(assets, argv[2], documents.path);
    app.runtime.SetState(Kotonoha::GameState::Gameplay);
    app.runtime.debugJumpToNode("0");
    const auto original = app.runtime.debugNode();
    auto row = [&] {
      check(app.runtime.panels().IsSaveLoadOpen(),
            "Retail SaveLoad did not open");
      for (int y = 0; y < 720; y += 4)
        for (int x = 0; x < 1280; x += 4)
          if (app.runtime.panels().SaveLoad().HitControl(x, y) == 0) {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = float(x);
            e.motion.y = float(y);
            app.runtime.onEvent(e);
            e = {};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = float(x);
            e.button.y = float(y);
            app.runtime.onEvent(e);
            return;
          }
      throw std::runtime_error("Cannot find retail slot 0");
    };
    auto dialog = [&]() -> Kotonoha::TextInputDialog * {
      for (auto *scene : app.runtime.GetActiveScenes())
        if (auto *d = dynamic_cast<Kotonoha::TextInputDialog *>(scene))
          return d;
      return nullptr;
    };
    Kotonoha::SchoolDaysSaveProfile profile;
    Kotonoha::SchoolDaysSaveStorage storage;
    std::string error;
    check(Kotonoha::SchoolDaysSaveProfile::Load(assets->root().string(),
                                                &profile, &error),
          error);
    check(storage.Initialize(documents.path, profile, &error), error);
    app.runtime.openPanel(10);
    row();
    check(dialog() != nullptr, "Retail save must request its name");
    SDL_Event cancel{};
    cancel.type = SDL_EVENT_KEY_DOWN;
    cancel.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(cancel);
    app.runtime.update();
    bool present = false;
    check(storage.ProbeSaveFile(0, &present, &error) && !present,
          "Cancel unexpectedly wrote a retail slot");
    app.runtime.openPanel(10);
    row();
    check(dialog() != nullptr, "Name dialog did not reopen");
    dialog()->submitText("Regression Save — teste");
    app.runtime.update();
    check(storage.ProbeSaveFile(0, &present, &error) && present,
          "Retail slot was not written: " + error);
    std::vector<uint8_t> bytes;
    check(storage.ReadSaveFile(0, &bytes, &error) && !bytes.empty(), error);
    app.runtime.openPanel(11);
    row();
    check(app.runtime.debugNode() == original,
          "Retail save/load changed the scene");
    // Exercise the original aspect buttons and repurposed SOMCON tab through
    // their actual retail hit maps, not a synthetic host-only setter.
    auto configControl = [&](int control) {
      check(app.runtime.panels().IsConfigOpen(),
            "Retail configuration did not open");
      const auto &model = app.runtime.panels().ConfigMenu();
      for (int y = 0; y < 720; y += 2)
        for (int x = 0; x < 1280; x += 2)
          if (model.HitControl(x, y) == control) {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = float(x);
            e.motion.y = float(y);
            app.runtime.onEvent(e);
            e = {};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = float(x);
            e.button.y = float(y);
            app.runtime.onEvent(e);
            return;
          }
      throw std::runtime_error("Missing retail config control");
    };
    app.runtime.openPanel(13);
    configControl(5);
    check(app.runtime.mediaArea().w == 960 && app.runtime.mediaArea().x == 160,
          "4:3 setting must change the content viewport");
    configControl(4);
    check(app.runtime.mediaArea().w == 1280,
          "16:9 setting must restore the viewport");
    configControl(2);
    check(!app.runtime.panels().IsConfigOpen() &&
              app.engine.GetSettingsMenu().isOpen(),
          "SOMCON replacement must open the Kotonoha settings component");
    app.engine.GetSettingsMenu().close();
    // A real ending invokes callback_38 and must present the native feeling
    // screen before the bridge acknowledges its terminal handoff.
    app.runtime.debugJumpToNode("1306");
    for (int i = 0; i < 4 && !app.runtime.panels().IsSaveFeelOpen(); ++i)
      app.runtime.openPanel(3);
    check(app.runtime.panels().IsSaveFeelOpen(),
          "Ending did not open its feeling screen");
    app.runtime.update();
    SDL_Delay(1550);
    app.runtime.update();
    app.runtime.update();
    check(app.runtime.panels().SaveFeel().Interactive(),
          "Feeling screen did not finish its native entry animation");
    SDL_Event skip{};
    skip.type = SDL_EVENT_KEY_DOWN;
    skip.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(skip);
    check(app.runtime.GetState() == Kotonoha::GameState::Menu,
          "Feeling acknowledgement did not finish terminal handoff");
    std::cout << "School Days retail save: cancel, named UTF-16 metadata, "
                 "write and reload passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
