#include "DemoGame.hpp"
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/SplashScreen.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct SaveSlot {
  std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("kotonoha-demo-" + std::to_string(SDL_GetPerformanceCounter()));
  std::filesystem::path path = directory / "quicksave.vn";
  SaveSlot() {
    require(std::filesystem::create_directory(directory), "Create test saves");
  }
  ~SaveSlot() {
    std::error_code error;
    for (const auto &entry :
         std::filesystem::directory_iterator(directory, error))
      std::filesystem::remove(entry.path(), error);
    std::filesystem::remove(directory, error);
  }
};

void press(Kotonoha::Engine &engine, SDL_Keycode key) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.windowID = SDL_GetWindowID(&engine.GetWindow());
  event.key.key = key;
  engine.Event(&event);
  engine.Iterate();
}

void click(Kotonoha::Engine &engine, float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.windowID = SDL_GetWindowID(&engine.GetWindow());
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  engine.Event(&event);
  engine.Iterate();
}

Kotonoha::VNSaveMenu *browser(Kotonoha::Runtime &game) {
  for (auto *scene : game.GetActiveScenes())
    if (auto *menu = dynamic_cast<Kotonoha::VNSaveMenu *>(scene))
      return menu;
  return nullptr;
}

void testMultipleSaves(KotonohaExample::DemoGame &game, Kotonoha::Engine &engine,
                       const SaveSlot &slot) {
  using namespace Kotonoha;
  VNSaveSlots storage(slot.directory.string());
  const auto originalQuick = VNSave::read(slot.path.string());
  auto &manager = GetVNManagerRuntime(engine);
  std::vector<std::string> paths;
  for (int i = 0; i < 40; ++i) {
    manager.debugJumpToNode(i % 2);
    const auto path = storage.nextPath();
    game.saveGame(path);
    paths.push_back(path);
  }
  require(storage.list().size() == 41,
          "Manual saves have no fixed 3/10/20-slot limit");
  for (std::size_t i = 0; i < paths.size(); ++i)
    require(VNSave::read(paths[i]).nodeId == static_cast<int>(i % 2),
            "Every independent slot persists its own node");
  game.loadGame(paths[0]);
  require(manager.getCurrentNodeId() == 0, "Load first of many saves");
  game.loadGame(paths[39]);
  require(manager.getCurrentNodeId() == 1, "Load last of many saves");

  const auto newPath = storage.nextPath();
  press(engine, SDLK_ESCAPE);
  click(engine, 500, 400); // Pause menu: Save.
  require(browser(game) && browser(game)->isOpen() && engine.isStoryPaused(),
          "Save browser opens as a story-pausing modal");
  press(engine, SDLK_F5);
  require(VNSave::read(slot.path.string()).nodeId == originalQuick.nodeId,
          "Quick save does not bypass the save browser");
  press(engine, SDLK_RETURN); // New save.
  require(!browser(game) && std::filesystem::exists(newPath) &&
              storage.list().size() == 42 && VNSave::read(newPath).nodeId == 1,
          "New save from UI creates another slot without overwriting");

  click(engine, 500, 400);
  press(engine, SDLK_DOWN); // First existing save (save-1.vn).
  press(engine, SDLK_RETURN);
  require(VNSave::read(paths[0]).nodeId == 0 && browser(game),
          "Selecting existing save requires confirmation");
  press(engine, SDLK_ESCAPE);
  require(browser(game), "Escape cancels overwrite, not the browser");
  press(engine, SDLK_RETURN);
  press(engine, SDLK_RETURN);
  require(!browser(game) && VNSave::read(paths[0]).nodeId == 1 &&
              storage.list().size() == 42,
          "Confirmed overwrite updates only the selected slot");
  require(VNSave::read(paths[2]).nodeId == 0 &&
              VNSave::read(slot.path.string()).nodeId == originalQuick.nodeId,
          "Other manual slots and quicksave stay unchanged");

  click(engine, 500, 460); // Pause menu: Load.
  require(browser(game), "Load browser opens from pause menu");
  for (int i = 0; i < 6; ++i)
    press(engine, SDLK_PAGEDOWN);
  press(engine, SDLK_RETURN); // Last entry: quicksave.vn.
  require(!browser(game) &&
              manager.getCurrentNodeId() == originalQuick.nodeId &&
              engine.GetTimeline().seconds() >= originalQuick.timelinePosition,
          "Paging reaches and restores the last save, including its clock");
  press(engine, SDLK_ESCAPE);
  click(engine, 500, 460);
  press(engine, SDLK_PAGEUP);
  press(engine, SDLK_ESCAPE);
  require(!browser(game), "Long list supports paging and cancellation");
  GetInGameMenuRuntime(engine).close();
  game.SetState(GameState::Menu);
  game.init(engine);
  press(engine, SDLK_DOWN);
  press(engine, SDLK_RETURN);
  require(browser(game) && game.GetState() == GameState::Menu,
          "Main menu load opens slot browser instead of loading quicksave");
  press(engine, SDLK_DOWN);
  press(engine, SDLK_DOWN); // save-3.vn at node 0.
  press(engine, SDLK_RETURN);
  require(!browser(game) && game.GetState() == GameState::Gameplay &&
              manager.getCurrentNodeId() == 0,
          "Select and restore an independent save from the main menu");

  const auto corrupt = slot.directory / "broken.vn";
  std::ofstream(corrupt) << "not a save";
  VNSaveMenu errors(engine);
  errors.open(slot.directory.string(), true);
  game.AddScene(&errors);
  const auto all = storage.list();
  const auto found =
      std::find_if(all.begin(), all.end(), [&](const VNSaveSlot &s) {
        return s.path == corrupt.string();
      });
  require(found != all.end(),
          "Corrupt saves remain visible, not silently hidden");
  for (auto i = all.begin(); i != found; ++i)
    press(engine, SDLK_DOWN);
  press(engine, SDLK_RETURN);
  require(errors.isOpen() && manager.getCurrentNodeId() == 0,
          "Invalid save reports error and leaves browser/current story intact");
  auto incompatible = VNSave::read(paths[0]);
  incompatible.nodeId = 987654;
  incompatible.write(corrupt.string());
  press(engine, SDLK_RETURN);
  require(errors.isOpen() && manager.getCurrentNodeId() == 0 &&
              browser(game) == &errors && engine.isStoryPaused(),
          "Failed restore rollback retains browser and prior story");
  errors.close();
  VNSaveMenu empty(engine);
  empty.open((slot.directory / "missing").string(), true);
  game.AddScene(&empty);
  press(engine, SDLK_RETURN);
  require(empty.isOpen() && manager.getCurrentNodeId() == 0,
          "Empty load list has no selectable phantom save");
  press(engine, SDLK_ESCAPE);
  require(!empty.isOpen(), "Empty load browser can be closed");
}

void testSaveManagement(KotonohaExample::DemoGame &game, Kotonoha::Engine &engine,
                        const SaveSlot &slot) {
  using namespace Kotonoha;
  const auto directory = slot.directory / "management";
  VNSaveSlots storage(directory.string());
  const auto original = storage.nextPath();
  game.saveGame(original);
  const auto saved = VNSave::read(original);
  const auto second = storage.nextPath();
  game.saveGame(second);
  auto rejects = [](auto action) {
    bool failed = false;
    try {
      action();
    } catch (const std::exception &) {
      failed = true;
    }
    require(failed, "Invalid file management operation must report failure");
  };
  for (const char *name :
       {"", " ", "../escape", "/absolute", "sub/file", "sub\\file", ".", "..",
        "trailing.", "trailing ", "bad:name", "bad\nname"})
    rejects([&] { storage.rename("save-1.vn", name); });
  rejects([&] { storage.rename("save-1.vn", "save-2"); });
  rejects([&] { storage.remove("../save-1.vn"); });
  rejects([&] { storage.remove("missing.vn"); });
  require(storage.rename("save-1.vn", "save-1.vn") == original,
          "Renaming a save to its own name is safe");
  const auto linked = directory / "linked.vn";
  std::filesystem::create_symlink(original, linked);
  rejects([&] { storage.remove("linked.vn"); });
  rejects([&] { storage.rename("linked.vn", "moved"); });
  require(std::filesystem::exists(original),
          "File management never follows symbolic links");
  std::filesystem::remove(linked);
  require(storage.list().size() == 2 &&
              VNSave::read(original).gameData == saved.gameData,
          "Invalid names and collisions preserve all save files");

  VNSaveMenu menu(engine);
  auto open = [&](bool load) {
    menu.open(directory.string(), load);
    game.AddScene(&menu);
    engine.Iterate();
  };
  auto text = [&](const char *value) {
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = SDL_GetWindowID(&engine.GetWindow());
    event.text.text = value;
    engine.Event(&event);
  };
  open(true);
  press(engine, SDLK_F2);
  require(SDL_TextInputActive(&engine.GetWindow()) && engine.isStoryPaused(),
          "Rename opens modal UTF-8 input and pauses the game");
  text("cancelled");
  press(engine, SDLK_ESCAPE);
  require(menu.isOpen() && !SDL_TextInputActive(&engine.GetWindow()) &&
              std::filesystem::exists(original),
          "Cancel rename preserves file and stops text input");
  press(engine, SDLK_F2);
  text("../escape");
  press(engine, SDLK_RETURN);
  require(menu.isOpen() && std::filesystem::exists(original),
          "Invalid rename reports error without losing the browser");

  click(engine, 400, 590); // Rename button.
  text("Viagem \xe9\x9b\xaa");
  press(engine, SDLK_RETURN);
  const auto renamed = directory / "Viagem \xe9\x9b\xaa.vn";
  require(menu.isOpen() && std::filesystem::exists(renamed) &&
              !std::filesystem::exists(original) &&
              !SDL_TextInputActive(&engine.GetWindow()) &&
              VNSave::read(renamed.string()).gameData == saved.gameData &&
              VNSave::read(renamed.string()).nodeId == saved.nodeId,
          "Rename button moves UTF-8 filename while preserving save contents");
  press(engine, SDLK_F2);
  text("save-2.vn");
  press(engine, SDLK_RETURN);
  require(std::filesystem::exists(renamed) && std::filesystem::exists(second) &&
              menu.isOpen(),
          "Rename collision is visible and never overwrites another save");
  press(engine, SDLK_DELETE);
  press(engine, SDLK_ESCAPE);
  require(std::filesystem::exists(renamed) && menu.isOpen(),
          "Escape cancels permanent deletion");
  click(engine, 750, 590); // Delete button.
  require(std::filesystem::exists(renamed),
          "Delete button requires confirmation");
  press(engine, SDLK_RETURN);
  require(!std::filesystem::exists(renamed) &&
              std::filesystem::exists(second) && storage.list().size() == 1,
          "Confirmed deletion removes only the selected save");
  press(engine, SDLK_DELETE);
  press(engine, SDLK_RETURN);
  require(storage.list().empty() && menu.isOpen(),
          "Deleting final save leaves a valid empty browser");
  press(engine, SDLK_F2);
  press(engine, SDLK_DELETE);
  press(engine, SDLK_RETURN);
  require(storage.list().empty(), "Empty list ignores rename/delete actions");
  menu.close();
  open(false);
  press(engine, SDLK_F2);
  press(engine, SDLK_DELETE);
  require(!SDL_TextInputActive(&engine.GetWindow()),
          "New-save placeholder cannot be renamed or deleted");
  press(engine, SDLK_RETURN);
  require(std::filesystem::exists(original),
          "Deleted numbered names are reusable for new saves");
  open(false);
  click(engine, 820, 230); // Rename existing row, not the new-save item.
  text("final.vn");
  press(engine, SDLK_RETURN);
  click(engine, 970, 230); // Delete the renamed row directly.
  press(engine, SDLK_RETURN);
  require(storage.list().empty(), "Save-mode browser also supports management");
  menu.close();
  std::filesystem::remove(directory);
}
} // namespace

int main() {
  try {
    SaveSlot slot;
    auto *environment = SDL_GetEnvironment();
    require(SDL_SetEnvironmentVariable(environment, "KOTONOHA_SAVE",
                                       slot.path.string().c_str(), true),
            "Set isolated save slot");
    for (const char *name : {"KOTONOHA_TIMELINE", "KOTONOHA_DEMO_VIDEO",
                             "KOTONOHA_DEMO_AUDIO", "KOTONOHA_TRANSLATION"})
      require(SDL_UnsetEnvironmentVariable(environment, name),
              "Isolate demo assets");
    KotonohaExample::DemoGame game;
    Kotonoha::Engine engine(game);
    auto &manager = Kotonoha::GetVNManagerRuntime(engine);
    engine.Iterate();
    require(game.GetState() == Kotonoha::GameState::Splash,
            "The demo opens with the engine splash");
    press(engine, SDLK_RETURN);
    require(game.GetState() == Kotonoha::GameState::Splash,
            "Keys do not skip the splash");
    click(engine, 100, 100);
    engine.Iterate();
    require(game.GetState() == Kotonoha::GameState::Menu,
            "A click skips the splash");
    game.SetState(Kotonoha::GameState::Splash);
    game.init(engine);
    engine.Iterate();
    SDL_Delay(200);
    engine.Iterate();
    require(game.GetState() == Kotonoha::GameState::Splash,
            "The splash remains visible before its timeout");
    const Uint64 waitUntil = SDL_GetTicks() +
        Uint64(Kotonoha::SplashScreen::displaySeconds * 1000) + 100;
    while (SDL_GetTicks() < waitUntil) {
      engine.Iterate();
      SDL_Delay(10);
    }
    engine.Iterate();
    require(game.GetState() == Kotonoha::GameState::Menu,
            "The splash advances automatically after its timeout");
    require(engine.GetDebug().getSettings().showFPS &&
                engine.GetDebug().getSettings().showNodeClock,
            "Diagnostics initialize in engine before gameplay");
    press(engine, SDLK_F9);
    require(!engine.GetDebug().getSettings().showFPS,
            "F9 toggles FPS without loading a save");
    press(engine, SDLK_F8);
    require(!engine.GetDebug().getSettings().showNodeClock,
            "F8 toggles node and clock");
    press(engine, SDLK_F9);
    press(engine, SDLK_F8);
    press(engine, SDLK_RETURN);
    require(game.GetState() == Kotonoha::GameState::Gameplay &&
                manager.getCurrentNodeId() == KotonohaExample::StoryNode::Opening,
            "New Game starts the mixed-mode demo");
    engine.GetTimeline().update(6);
    engine.Iterate();
    require(manager.getCurrentNodeId() == KotonohaExample::StoryNode::Camp,
            "Timed intro transitions to traditional scene");
    for (int i = 0; i < 4; ++i)
      press(engine, SDLK_SPACE);
    require(manager.getCurrentNodeId() == KotonohaExample::StoryNode::AskName,
            "Traditional dialogue opens name input");
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.windowID = SDL_GetWindowID(&engine.GetWindow());
    text.text.text = "Alice";
    engine.Event(&text);
    press(engine, SDLK_RETURN);
    require(manager.getCurrentNodeId() == KotonohaExample::StoryNode::Greeting,
            "Name modal returns to the story graph");
    press(engine, SDLK_SPACE);
    press(engine, SDLK_SPACE);
    press(engine, SDLK_RETURN);
    press(engine, SDLK_SPACE);
    press(engine, SDLK_SPACE);
    require(manager.getCurrentNodeId() == KotonohaExample::StoryNode::Storm,
            "Equipment route enters timed storm");
    engine.GetTimeline().update(2);
    press(engine, SDLK_F5);
    const auto saved = Kotonoha::VNSave::read(slot.path.string());
    require(saved.nodeId == KotonohaExample::StoryNode::Storm &&
                saved.timelinePosition >= 2 &&
                saved.gameData.find("Alice") != std::string::npos,
            "F5 persists actual demo node, clock and game state");
    manager.debugJumpToNode(KotonohaExample::StoryNode::Escape);
    engine.Iterate();
    press(engine, SDLK_F6);
    require(manager.getCurrentNodeId() == KotonohaExample::StoryNode::Storm &&
                engine.GetTimeline().seconds() >= saved.timelinePosition,
            "F6 restores the demo at the saved timeline position");
    press(engine, SDLK_F10);
    require(engine.GetDebug().isVisible() && engine.GetTimeline().isPaused(),
            "ImGui inspector pauses actual demo scene");
    manager.debugJumpToNode(KotonohaExample::StoryNode::Cave);
    engine.Iterate();
    press(engine, SDLK_F10);
    engine.GetTimeline().update(5);
    engine.Iterate();
    require(manager.getCurrentNodeId() == 14,
            "Loading demo preserves torch inventory used by conditional route");
    testMultipleSaves(game, engine, slot);
    testSaveManagement(game, engine, slot);
    std::cout << "Demo integration tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
