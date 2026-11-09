#include <Kirikiri/KirikiriRuntime.hpp>
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace Kirikiri {

namespace {

std::string scenarioTitle(const std::string &fileName) {
  auto p = std::filesystem::path(fileName);
  return p.stem().string();
}

} // namespace

struct KirikiriRuntime::Impl {
  KirikiriRuntime &runtime;
  std::shared_ptr<KirikiriAssets> assets;
  Kotonoha::Engine *engine = nullptr;

  // Presentation state
  std::string currentScenario;
  std::unique_ptr<KagScript::Runner> runner;
  std::string dialogueSpeaker;
  std::string dialogueText;
  bool dialogueVisible = false;

  // Scenes
  Kotonoha::VNManager *manager = nullptr;
  Kotonoha::VNTextBox *textbox = nullptr;
  Kotonoha::ChoiceQuestionScene *choiceScene = nullptr;
  Kotonoha::VNBacklog *backlog = nullptr;
  Kotonoha::InGameMenu *menu = nullptr;
  Kotonoha::VNSaveMenu *saveMenu = nullptr;

  bool running = false;
  bool waitingChoice = false;
  bool waitingWait = false;

  Impl(KirikiriRuntime &r, std::shared_ptr<KirikiriAssets> a)
      : runtime(r), assets(std::move(a)) {}
};

KirikiriRuntime::KirikiriRuntime(std::shared_ptr<KirikiriAssets> assets)
    : Runtime("Kirikiri", "0.1.0", "me.hirameki.kotonoha.kirikiri"),
      impl_(std::make_unique<Impl>(*this, std::move(assets))) {}

KirikiriRuntime::~KirikiriRuntime() = default;

void KirikiriRuntime::init(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.engine = &engine;
  Runtime::init(engine);
}

void KirikiriRuntime::initGlobal(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.manager = &Kotonoha::GetVNManagerRuntime(engine);
  impl.textbox = &Kotonoha::GetVNTextBoxRuntime(engine, "", "", 30);
  impl.choiceScene = &Kotonoha::GetChoiceQuestionRuntime(engine);
  impl.backlog = &Kotonoha::GetVNBacklogRuntime(engine);
  impl.menu = &Kotonoha::GetInGameMenuRuntime(engine);
  impl.saveMenu = new Kotonoha::VNSaveMenu(engine);

  auto &menu = *impl.menu;
  menu.SetOnReturnToMenu([this] { SetState(Kotonoha::GameState::Menu); });
  menu.SetOnSave([this] {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    saveGame(path.string());
  });
  menu.SetOnLoad([this] {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    loadGame(path.string());
  });
}

void KirikiriRuntime::initSplash(Kotonoha::Engine &engine) {
  // Kirikiri games go directly to gameplay; no engine splash screen.
  SetState(Kotonoha::GameState::Gameplay);
  initGameplay(engine);
}

void KirikiriRuntime::initMenu(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  impl.menu->open();
  impl.menu->SetOnResume([this] {
    SetState(Kotonoha::GameState::Gameplay);
    init(*impl_->engine);
  });
  AddScene(impl.menu);
}

void KirikiriRuntime::initGameplay(Kotonoha::Engine &engine) {
  auto &impl = *impl_;
  SDL_Log("Kirikiri: initGameplay called");
  Kotonoha::GetVNBacklogRuntime(engine).clear();
  Kotonoha::GetVNBacklogRuntime(engine).close();

  AddScene(impl.textbox);
  AddScene(impl.choiceScene);
  AddScene(impl.backlog);
  AddScene(impl.menu);

  if (impl.runner && !impl.runner->isFinished()) {
    // Resume a running scenario.
    impl.running = true;
    impl.runner->run();
  } else if (impl.currentScenario.empty()) {
    // Auto-start the first scenario found in the data.
    auto scenarios = impl.assets->scenarioFiles();
    SDL_Log("Kirikiri: found %zu scenarios", scenarios.size());
    for (const auto &s : scenarios)
      SDL_Log("Kirikiri: scenario: %s", s.c_str());
    if (!scenarios.empty())
      startScenario(scenarios.front());
  } else {
    startScenario(impl.currentScenario);
  }
}

void KirikiriRuntime::update() {
  Runtime::update();
  auto &impl = *impl_;
  if (impl.engine && impl.running && !impl.runner->isBlocked() &&
      !impl.runner->isFinished() && !impl.engine->isStoryPaused()) {
    impl.runner->run();
  }
}

void KirikiriRuntime::renderGameplay(Kotonoha::RendererRef renderer,
                                     float scale) {
  // Let the base class render all gameplay scenes (textbox, choices, etc.).
  Runtime::renderGameplay(renderer, scale);
}

void KirikiriRuntime::advanceDialogue() {
  auto &impl = *impl_;
  SDL_Log("Kirikiri: advanceDialogue start");
  if (impl.dialogueVisible && impl.runner && impl.runner->isBlocked()) {
    impl.dialogueVisible = false;
    impl.runner->setBlocked(false);
    SDL_Log("Kirikiri: advanceDialogue calling runner->run()");
    impl.runner->run();
    SDL_Log("Kirikiri: advanceDialogue after run, blocked=%d finished=%d",
            impl.runner->isBlocked() ? 1 : 0,
            impl.runner->isFinished() ? 1 : 0);
  } else {
    SDL_Log("Kirikiri: advanceDialogue conditions not met");
  }
}

bool KirikiriRuntime::handleAdvanceAction() {
  auto &impl = *impl_;
  SDL_Log("Kirikiri: handleAdvanceAction dialogueVisible=%d runner=%d",
          impl.dialogueVisible ? 1 : 0, impl.runner ? 1 : 0);
  if (impl.dialogueVisible && impl.runner && impl.runner->isBlocked()) {
    SDL_Log("Kirikiri: handleAdvanceAction advancing dialogue");
    advanceDialogue();
    SDL_Log(
        "Kirikiri: handleAdvanceAction after advance, blocked=%d finished=%d",
        impl.runner->isBlocked() ? 1 : 0, impl.runner->isFinished() ? 1 : 0);
    return true;
  }
  SDL_Log("Kirikiri: handleAdvanceAction returning false");
  return false;
}

bool KirikiriRuntime::onEvent(Kotonoha::EventRef event) {
  auto mapped = Kotonoha::MapControllerEvent(event);
  auto &impl = *impl_;
  if (impl.dialogueVisible && (mapped.type == SDL_EVENT_KEY_DOWN ||
                               mapped.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                               mapped.type == SDL_EVENT_FINGER_DOWN)) {
    if (mapped.type != SDL_EVENT_KEY_DOWN || !mapped.key.repeat) {
      advanceDialogue();
      return true;
    }
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F5) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    saveGame(path.string());
    return true;
  }
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat &&
      mapped.key.key == SDLK_F6) {
    auto path =
        Kotonoha::preferredDocumentsDirectory("kirikiri") / "quicksave.kns";
    loadGame(path.string());
    return true;
  }
  return Runtime::onEvent(mapped);
}

void KirikiriRuntime::saveGame(const std::string &path) {
  auto &impl = *impl_;
  std::ostringstream data;
  data << "KIRIKIRI_SAVE_2\n" << impl.currentScenario << '\n';
  data << (impl.runner ? impl.runner->position() : 0) << '\n';
  Kotonoha::GetVNManagerRuntime(*impl.engine).save(path, data.str());
}

void KirikiriRuntime::loadGame(const std::string &path) {
  auto &impl = *impl_;
  auto saved = Kotonoha::VNSave::read(path);
  std::istringstream data(saved.gameData);
  std::string header, scenario;
  std::size_t position = 0;
  std::getline(data, header);
  if (header == "KIRIKIRI_SAVE_2") {
    std::getline(data, scenario);
    data >> position;
  } else {
    // Version 1 only stored the scenario name and always resumed at its start.
    scenario = std::move(header);
  }
  if (!scenario.empty())
    startScenario(scenario, position);
}

bool KirikiriRuntime::startScenario(const std::string &name,
                                    std::size_t position) {
  SDL_Log("Kirikiri: startScenario ENTRY name='%s'", name.c_str());
  auto &impl = *impl_;
  auto files = impl.assets->scenarioFiles();
  std::string target = name;
  bool found = false;
  for (const auto &f : files) {
    if (f == name || std::filesystem::path(f).filename().string() == name ||
        scenarioTitle(f) == scenarioTitle(name)) {
      target = f;
      found = true;
      break;
    }
  }
  if (!found)
    return false;

  auto bytes = impl.assets->readAsset(target);
  std::string text(bytes.begin(), bytes.end());
  SDL_Log("Kirikiri: startScenario '%s' read %zu bytes", target.c_str(),
          text.size());
  auto script = KagScript::parse(text, target);
  SDL_Log("Kirikiri: parsed %zu tags", script.tags.size());

  impl.currentScenario = target;
  impl.runner = std::make_unique<KagScript::Runner>(
      script,
      KagScript::Callbacks{
          .showText =
              [this](std::string speaker, std::string text) {
                dialogueSpeaker_ = std::move(speaker);
                dialogueText_ = std::move(text);
                auto &impl = *impl_;
                impl.dialogueVisible = true;
              },
          .showChoice =
              [this](std::string question, std::vector<std::string> options,
                     std::function<void(int)> onChoose) {
                auto &impl = *impl_;
                impl.waitingChoice = true;
                std::vector<Kotonoha::ChoiceOption> choiceOptions;
                int id = 0;
                for (auto &opt : options)
                  choiceOptions.push_back({opt, id++});
                impl.choiceScene->setQuestion(
                    question, choiceOptions,
                    [this, onChoose = std::move(onChoose)](
                        int optionId, const std::string &) {
                      auto &impl = *impl_;
                      impl.waitingChoice = false;
                      impl.runner->setBlocked(false);
                      onChoose(optionId);
                      impl.runner->run();
                    });
              },
          .jump =
              [this](std::string label) {
                auto &impl = *impl_;
                if (!label.empty())
                  debugJumpToLabel(label);
              },
          .wait =
              [this](int ms, std::function<void()> onDone) {
                auto &impl = *impl_;
                impl.waitingWait = true;
                SDL_AddTimer(
                    static_cast<Uint32>(ms),
                    [](void *userdata, Uint32, Uint32) -> Uint32 {
                      auto self = static_cast<KirikiriRuntime *>(userdata);
                      self->impl_->waitingWait = false;
                      self->impl_->runner->setBlocked(false);
                      self->impl_->runner->resumeWait();
                      return 0;
                    },
                    this);
              },
          .showBg =
              [this](std::string file, int transitionMs) {
                SDL_Log("Kirikiri: bgshow %s (transition %dms)", file.c_str(),
                        transitionMs);
              },
          .showChar =
              [this](std::string layer, std::string file, int x, int y,
                     int transitionMs) {
                SDL_Log("Kirikiri: chara_show layer=%s file=%s x=%d y=%d",
                        layer.c_str(), file.c_str(), x, y);
              },
          .hideChar =
              [this](std::string layer, int transitionMs) {
                SDL_Log("Kirikiri: chara_hide layer=%s", layer.c_str());
              },
          .playBgm =
              [this](std::string file, bool loop) {
                SDL_Log("Kirikiri: playbgm %s loop=%d", file.c_str(), loop);
              },
          .playSe =
              [this](std::string file) {
                SDL_Log("Kirikiri: playse %s", file.c_str());
              },
          .stopBgm = [this]() { SDL_Log("Kirikiri: stopbgm"); },
          .setVar =
              [this](std::string name, std::string value) {
                SDL_Log("Kirikiri: set %s=%s", name.c_str(), value.c_str());
              },
          .getVar = [this](std::string name) -> std::string { return {}; },
      });
  impl.runner->restorePosition(position);
  impl.running = true;
  impl.runner->run();
  return true;
}

bool KirikiriRuntime::debugJumpToLabel(const std::string &label) {
  auto &impl = *impl_;
  if (!impl.runner)
    return false;
  if (!impl.runner->jumpToLabel(label))
    return false;
  impl.runner->run();
  return true;
}

std::string KirikiriRuntime::debugNode() const {
  auto &impl = *impl_;
  return impl.currentScenario.empty()
             ? std::string()
             : ("kirikiri | scenario " + impl.currentScenario);
}

void KirikiriRuntime::debugRestartScene() {
  if (impl_->currentScenario.empty() || !startScenario(impl_->currentScenario))
    throw std::runtime_error("There is no active Kirikiri scene to restart");
}

} // namespace Kirikiri
