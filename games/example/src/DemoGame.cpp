#include "DemoGame.hpp"
#include "DemoStage.hpp"

#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace KotonohaExample {

DemoGame::DemoGame() : Runtime("Kotonoha", "0.1.0", "me.hirameki.kotonoha") {}
DemoGame::~DemoGame() = default;

void DemoGame::init(Kotonoha::Engine &engineRef) {
  engine = &engineRef;
  Runtime::init(engineRef);
}

void DemoGame::initMenu(Kotonoha::Engine &engineRef) {
  auto &menu = Kotonoha::GetGameMenuRuntime(engineRef);
  menu.open();
  menu.setOnResume([this] { openSaves(true); });
  menu.setOnQuit([] {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
  });
  AddScene(&menu);
}

void DemoGame::initGameplay(Kotonoha::Engine &engineRef) {
  state = pendingLoad ? restoredState : DemoState{};
  Kotonoha::GetVNBacklogRuntime(engineRef).clear();
  Kotonoha::GetVNBacklogRuntime(engineRef).close();
  Kotonoha::GetInGameMenuRuntime(engineRef).close();
  auto &manager = Kotonoha::GetVNManagerRuntime(engineRef);
  if (!stage)
    stage = std::make_unique<DemoStage>(engineRef);
  AddScene(stage.get());
  AddScene(&manager);
  AddScene(&Kotonoha::GetVNTextBoxRuntime(engineRef, "", "", 30));
  AddScene(&Kotonoha::GetChoiceQuestionRuntime(engineRef));
  AddScene(&Kotonoha::GetVNBacklogRuntime(engineRef));
  auto &menu = Kotonoha::GetInGameMenuRuntime(engineRef);
  menu.SetOnReturnToMenu([this] {
    SetState(Kotonoha::GameState::Menu);
    init(*engine);
  });
  menu.SetOnSave([this] { openSaves(false); });
  menu.SetOnLoad([this] { openSaves(true); });
  AddScene(&menu);
  manager.loadScript(buildDemoStory(state, [this] { requestPlayerName(); }));
  manager.setVariable("player", state.playerName);
  if (pendingLoad) {
    const auto saved = *pendingLoad;
    pendingLoad.reset();
    manager.restoreSave(saved);
  } else {
    manager.startScript(StoryNode::Opening);
  }
}

void DemoGame::requestPlayerName() {
  auto &manager = Kotonoha::GetVNManagerRuntime(*engine);
  manager.setInputBlocked(true);
  auto &dialog = Kotonoha::GetTextInputDialogRuntime(
      *engine,
      engine->GetTranslation().text(
          StoryNode::AskName, manager.getScriptNode(StoryNode::AskName).text),
      [this](const std::string &input) {
        const auto first = input.find_first_not_of(" \t\r\n");
        const auto last = input.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "The name cannot be empty.");
          return;
        }
        state.playerName = input.substr(first, last - first + 1);
        auto &manager = Kotonoha::GetVNManagerRuntime(*engine);
        manager.setVariable("player", state.playerName);
        engine->GetRuntime().RemoveScene(&Kotonoha::GetTextInputDialogRuntime(
            *engine, "", nullptr, nullptr));
        manager.setInputBlocked(false);
        manager.jumpToNode(StoryNode::Greeting);
      },
      [] {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Enter a name to continue.");
      });
  AddScene(&dialog);
}

std::string DemoGame::savePath() const {
  if (const char *path = SDL_getenv("KOTONOHA_SAVE"))
    return path;
  return (Kotonoha::preferredDocumentsDirectory("Kotonoha") / "quicksave.vn").string();
}

void DemoGame::openSaves(bool load) {
  if (!saveMenu)
    saveMenu = std::make_unique<Kotonoha::VNSaveMenu>(*engine);
  const auto parent = std::filesystem::path(savePath()).parent_path();
  saveMenu->open(parent.empty() ? "." : parent.string(), load);
  RemoveScene(saveMenu.get());
  AddScene(saveMenu.get());
}

void DemoGame::saveGame(const std::string &path) {
  std::ostringstream data;
  data << std::quoted(state.playerName) << ' ' << state.hasTorch << ' '
       << state.hasMap << ' ' << state.trustLevel;
  Kotonoha::GetVNManagerRuntime(*engine).save(path, data.str());
  SDL_Log("Game saved at the current node.");
}

void DemoGame::loadGame(const std::string &path) {
  auto saved = Kotonoha::VNSave::read(path);
  std::istringstream data(saved.gameData);
  DemoState loaded;
  int torch = 0, map = 0;
  if (!(data >> std::quoted(loaded.playerName) >> torch >> map >>
        loaded.trustLevel) ||
      (torch != 0 && torch != 1) || (map != 0 && map != 1))
    throw std::runtime_error("Invalid demo state in save");
  data >> std::ws;
  if (!data.eof())
    throw std::runtime_error("Unexpected saved demo data");
  loaded.hasTorch = torch != 0;
  loaded.hasMap = map != 0;
  const auto previousState = GetState();
  const auto previousGame = state;
  std::optional<Kotonoha::VNSave> previousSave;
  auto &manager = Kotonoha::GetVNManagerRuntime(*engine);
  if (previousState == Kotonoha::GameState::Gameplay && !manager.isFinished())
    previousSave = manager.captureSave();
  restoredState = std::move(loaded);
  pendingLoad = std::move(saved);
  SetState(Kotonoha::GameState::Gameplay);
  try {
    init(*engine);
  } catch (const std::exception &error) {
    const std::string failure = error.what();
    pendingLoad.reset();
    SetState(previousState);
    if (previousSave) {
      restoredState = previousGame;
      pendingLoad = *previousSave;
    }
    try {
      init(*engine);
    } catch (const std::exception &rollback) {
      throw std::runtime_error(failure +
                               "; rollback failed: " + rollback.what());
    }
    throw;
  }
}

void DemoGame::saveLoadAction(bool load) {
  try {
    if (load)
      loadGame(savePath());
    else
      saveGame(savePath());
  } catch (const std::exception &error) {
    pendingLoad.reset();
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save/load failed: %s",
                 error.what());
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save / load",
                                  error.what(), &engine->GetWindow()))
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot show save error: %s",
                   SDL_GetError());
  }
}

void DemoGame::onEvent(Kotonoha::EventRef event) {
  if (GetState() == Kotonoha::GameState::Gameplay &&
      !(saveMenu && saveMenu->isOpen() &&
        std::find(GetActiveScenes().begin(), GetActiveScenes().end(),
                  saveMenu.get()) != GetActiveScenes().end()) &&
      event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
      (event.key.key == SDLK_F5 || event.key.key == SDLK_F6)) {
    saveLoadAction(event.key.key == SDLK_F6);
    return;
  }
  Runtime::onEvent(event);
}

void DemoGame::update() {
  Runtime::update();
  if (GetState() == Kotonoha::GameState::Gameplay && !engine->isStoryPaused() &&
      Kotonoha::GetVNManagerRuntime(*engine).isFinished()) {
    SetState(Kotonoha::GameState::Menu);
    init(*engine);
  }
}

} // namespace KotonohaExample
