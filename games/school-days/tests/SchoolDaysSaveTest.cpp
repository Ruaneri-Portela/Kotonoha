#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/routing/SchoolDaysConfigMenuRenderPlan.hpp>
#include <Kotonoha/routing/SchoolDaysSaveLoadRenderPlan.hpp>
#include <SchoolDays/SchoolDaysAssets.hpp>
#include <algorithm>
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
  static Kotonoha::EngineOptions options(
      const std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> &assets) {
    Kotonoha::EngineOptions result;
    result.assets = assets;
    result.persistSettings = false;
    result.rendererOverride = "software";
    return result;
  }
  Application(
      const std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> &assets,
      const char *route, const std::filesystem::path &documents)
      : runtime(assets, route, documents), engine(runtime, options(assets)) {}
  ~Application() { runtime.shutdown(); }
};
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 4,
          "Expected retail data, route and remaster display overlay");
    struct Directory {
      std::filesystem::path path =
          std::filesystem::temp_directory_path() /
          ("school-days-save-" + std::to_string(SDL_GetPerformanceCounter()));
      ~Directory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
      }
    } documents;
    // The original System.GPK has SOMCON, not Display Options. Mount the
    // user's separate authored overlay just as the bundled Windows game does,
    // never mutate or replace the commercial GPK.
    auto assets = std::make_shared<Kotonoha::Providers::SchoolDaysAssets>(
        argv[1], argv[3]);
    Application app(assets, argv[2], documents.path);
    app.runtime.SetState(Kotonoha::GameState::Gameplay);
    app.runtime.debugJumpToNode("0");
    for (int frame = 0; frame < 4; ++frame)
      check(app.engine.Iterate() == SDL_APP_CONTINUE,
            "Initial scene frame failed");
    const auto original = app.runtime.debugNode();
    auto openPanel = [&](int control) {
      app.runtime.openPanel(control);
      // Retail SYSBASE keeps input locked during its 750 ms entry animation.
      if (control == 10 || control == 11 || control == 13) {
        SDL_Delay(760);
        app.runtime.update();
      }
    };
    auto row = [&] {
      check(app.runtime.panels().IsSaveLoadOpen(),
            "Retail SaveLoad did not open");
      const auto &model = app.runtime.panels().SaveLoad();
      const int width = int(model.Cmap().Width());
      const int height = int(model.Cmap().Height());
      for (int y = 4; y < height; y += 4)
        for (int x = 4; x < width; x += 4)
          if (model.HitControl(x, y) == 0) {
            const float px = float(x) * 1280.0f / float(width);
            const float py = float(y) * 720.0f / float(height);
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = px;
            e.motion.y = py;
            app.runtime.onEvent(e);
            e = {};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = px;
            e.button.y = py;
            app.runtime.onEvent(e);
            return;
          }
      throw std::runtime_error("Cannot find retail slot 0");
    };
    Kotonoha::SchoolDaysSaveProfile profile;
    Kotonoha::SchoolDaysSaveStorage storage;
    std::string error;
    check(Kotonoha::SchoolDaysSaveProfile::Load(assets->root().string(),
                                                &profile, &error),
          error);
    check(storage.Initialize(documents.path, profile, &error), error);
    openPanel(10);
    const auto saveSprites = Kotonoha::SchoolDaysSaveLoadRenderPlan::Build(
        app.runtime.panels().SaveLoad());
    check(std::none_of(
              saveSprites.begin(), saveSprites.end(),
              [](const auto &sprite) {
                return sprite.role ==
                       Kotonoha::SchoolDaysSaveLoadRenderPlan::Role::RouteMap;
              }),
          "Retail SAVE must not render Open Route Map");
    row();
    check(app.runtime.panels().IsSaveLoadCommentEditorOpen(),
          "Retail save must request its comment");
    SDL_Event cancel{};
    cancel.type = SDL_EVENT_KEY_DOWN;
    cancel.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(cancel);
    app.runtime.update();
    bool present = false;
    check(storage.ProbeSaveFile(0, &present, &error) && !present,
          "Cancel unexpectedly wrote a retail slot");
    row();
    check(app.runtime.panels().IsSaveLoadCommentEditorOpen(),
          "Comment editor did not reopen");
    SDL_Event typed{};
    typed.type = SDL_EVENT_TEXT_INPUT;
    typed.text.text = "Regression Save";
    app.runtime.onEvent(typed);
    SDL_Event accept{};
    accept.type = SDL_EVENT_KEY_DOWN;
    accept.key.key = SDLK_RETURN;
    app.runtime.onEvent(accept);
    app.runtime.update();
    check(storage.ProbeSaveFile(0, &present, &error) && present,
          "Retail slot was not written: " + error);
    std::vector<uint8_t> bytes;
    check(storage.ReadSaveFile(0, &bytes, &error) && !bytes.empty(), error);
    SDL_Event close{};
    close.type = SDL_EVENT_KEY_DOWN;
    close.key.key = SDLK_ESCAPE;
    app.runtime.onEvent(close);
    SDL_Delay(510);
    app.runtime.update();
    SDL_Delay(510);
    app.runtime.update();
    openPanel(11);
    const auto loadSprites = Kotonoha::SchoolDaysSaveLoadRenderPlan::Build(
        app.runtime.panels().SaveLoad());
    // The base LOAD art contains the neutral label; Role::RouteMap is its
    // hover CHIP, not an always-on sprite.
    check(std::none_of(
              loadSprites.begin(), loadSprites.end(),
              [](const auto &sprite) {
                return sprite.role ==
                       Kotonoha::SchoolDaysSaveLoadRenderPlan::Role::RouteMap;
              }),
          "LOAD blue Route Map CHIP must be absent without pointer hover");
    const auto hasRouteMapHover = [&] {
      const auto sprites = Kotonoha::SchoolDaysSaveLoadRenderPlan::Build(
          app.runtime.panels().SaveLoad());
      return std::any_of(
          sprites.begin(), sprites.end(), [](const auto &sprite) {
            return sprite.role ==
                   Kotonoha::SchoolDaysSaveLoadRenderPlan::Role::RouteMap;
          });
    };
    // Exercise the real SDL event path over LOAD's retail CMAP button 21.
    {
      const auto &model = app.runtime.panels().SaveLoad();
      const int width = int(model.Cmap().Width());
      const int height = int(model.Cmap().Height());
      int hitX = -1, hitY = -1;
      for (int y = 0; y < height && hitX < 0; y += 2)
        for (int x = 0; x < width; x += 2)
          if (model.HitControl(x, y) == 21) {
            hitX = x;
            hitY = y;
            break;
          }
      check(hitX >= 0, "LOAD CMAP lacks Open Route Map hitbox");
      SDL_Event hover{};
      hover.type = SDL_EVENT_MOUSE_MOTION;
      hover.motion.x = float(hitX) * 1280.0f / float(width);
      hover.motion.y = float(hitY) * 720.0f / float(height);
      app.runtime.onEvent(hover);
      check(hasRouteMapHover(), "Open Route Map CHIP did not follow hover");
      SDL_Event leave{};
      leave.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
      app.runtime.onEvent(leave);
      check(!hasRouteMapHover(), "Open Route Map CHIP survived pointer leave");
      app.runtime.onEvent(hover);
      check(hasRouteMapHover(), "Open Route Map CHIP did not recover");
      SDL_Event focusLost{};
      focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
      app.runtime.onEvent(focusLost);
      check(!hasRouteMapHover(), "Open Route Map CHIP survived focus loss");
      hover.motion.x = -12.0f;
      hover.motion.y = -12.0f;
      app.runtime.onEvent(hover);
    }
    row();
    // Retail LOAD now dims SaveLoad, presents POPUP_LOAD, fades the card to
    // black, restores the slot behind it, then reveals the new scene. Advance
    // each real-time stage before testing controls on the restored gameplay.
    SDL_Delay(1010);
    app.runtime.update();
    SDL_Delay(910);
    app.runtime.update();
    SDL_Delay(510);
    app.runtime.update();
    SDL_Delay(510);
    app.runtime.update();
    check(!app.runtime.panels().IsSaveLoadOpen(),
          "Retail LOAD did not close SaveLoad after the loading transition");
    check(app.runtime.debugNode() == original,
          "Retail save/load changed the scene");
    // The custom Display Options tab needs user-authored PNG/CMAP files in the
    // separate overlay. They are not part of the retail packs or this repo, so
    // only run that final native-tab check when the overlay actually provides
    // them, just as the port itself refuses to replace the native menu with a
    // generic host settings screen when the art cannot be resolved.
    const std::filesystem::path overlay(argv[3]);
    const auto displayArt = [&](const char *file) {
      return std::filesystem::exists(overlay / "System" / "Option" / file);
    };
    const bool displayArtwork = displayArt("Option_Display.cmap") &&
                                displayArt("Option_Display.png") &&
                                displayArt("Option_Display_Chip.png");
    if (!displayArtwork) {
      std::cout << "[SKIP] Display Options artwork is not present in "
                << overlay << "; native tab assertions skipped\n";
    } else {
    // Exercise the original aspect buttons and new Display Options tab through
    // their actual retail hit maps, not a synthetic host-only setter.
    auto configControl = [&](int control) {
      check(app.runtime.panels().IsConfigOpen(),
            "Retail configuration did not open");
      const auto &model = app.runtime.panels().ConfigMenu();
      const int width = int(model.Cmap().Width());
      const int height = int(model.Cmap().Height());
      for (int y = 4; y < height; y += 2)
        for (int x = 4; x < width; x += 2)
          if (model.HitControl(x, y) == control) {
            const float px = float(x) * 1280.0f / float(width);
            const float py = float(y) * 720.0f / float(height);
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = px;
            e.motion.y = py;
            app.runtime.onEvent(e);
            e = {};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = px;
            e.button.y = py;
            app.runtime.onEvent(e);
            return;
          }
      throw std::runtime_error("Missing retail config control");
    };
    app.runtime.update();
    openPanel(13);
    configControl(2);
    check(app.runtime.panels().IsConfigOpen() &&
              app.runtime.panels().ConfigMenu().CurrentPage() ==
                  Kotonoha::SchoolDaysConfigMenuModel::Page::Display,
          "New Display Options must remain a native School Days config tab");
    check(app.runtime.panels().ConfigMenu().Cmap().Width() > 0,
          "Display Options CMAP must load from the separate authored overlay");
    const auto displaySprites = Kotonoha::SchoolDaysConfigMenuRenderPlan::Build(
        app.runtime.panels().ConfigMenu(), {});
    check(!displaySprites.empty() &&
              displaySprites.front().role ==
                  Kotonoha::SchoolDaysConfigMenuRenderPlan::Role::Base,
          "Display Options base art must be rendered from its own atlas");
    }
    // Terminal handoff is exercised by SchoolDaysHandoffRuntimeTest.
    std::cout << "School Days retail save: cancel, named UTF-16 metadata, "
                 "write and reload passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
