#include "DdlcRuntime.hpp"
#include "DdlcSession.hpp"

#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/GameMenu.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <stdexcept>

namespace Ddlc {
namespace {

class Stage final : public Kotonoha::Scene {
public:
  Stage(Kotonoha::Engine &engine, GameSession &session)
      : Scene(engine), session_(session) {}
  void render(Kotonoha::RendererRef renderer, float) override {
    session_.drawStage(renderer);
  }
  void translationChanged(const std::string &original,
                          const std::string &previous,
                          const std::string &translated) override {
    session_.translationChanged(original, previous, translated);
  }

private:
  GameSession &session_;
};

class Frontend final : public Kotonoha::GameMenu {
public:
  Frontend(Kotonoha::Engine &engine, GameSession &session)
      : GameMenu(engine), session_(session) {}
  bool isVisible() const override { return session_.inMenu(); }
  bool handleEvent(Kotonoha::EventRef event) override {
    return isVisible() && session_.event(event);
  }

protected:
  int itemCount() const override { return 0; }
  void renderTitle(Kotonoha::RendererRef renderer) override {
    session_.drawMenu(renderer);
  }

private:
  GameSession &session_;
};

class Dialogue final : public Kotonoha::VNTextBox {
public:
  Dialogue(Kotonoha::Engine &engine, GameSession &session)
      : VNTextBox(engine, "", ""), session_(session) {}
  bool handleEvent(Kotonoha::EventRef event) override {
    return !session_.inMenu() && session_.event(event);
  }
  void update() override {
    session_.showingDialogue() ? show() : hide();
    speakerName = session_.dialogueSpeaker();
    fullDialogue = session_.dialogueText();
    visibleDialogue = session_.visibleDialogueText();
    isComplete = session_.dialogueComplete();
  }
  void completeDialogue() override {
    session_.completeDialogue();
    update();
  }

protected:
  void renderFrame(Kotonoha::RendererRef renderer) override {
    session_.drawDialogue(renderer);
  }
  void renderSpeaker(Kotonoha::RendererRef) override {}
  void renderDialogue(Kotonoha::RendererRef) override {}
  void renderIndicator(Kotonoha::RendererRef) override {}

private:
  GameSession &session_;
};

class Choices final : public Kotonoha::ChoiceQuestionScene {
public:
  Choices(Kotonoha::Engine &engine, GameSession &session)
      : ChoiceQuestionScene(engine, "", {}, nullptr), session_(session) {}
  bool capturesInput() const override {
    return session_.choosing() && !session_.paused();
  }
  bool onEvent(Kotonoha::EventRef event) override {
    if (capturesInput()) {
      session_.event(event);
      return true;
    }
    return false;
  }
  void render(Kotonoha::RendererRef renderer, float) override {
    session_.drawChoices(renderer);
  }

private:
  GameSession &session_;
};

class Pause final : public Kotonoha::InGameMenu {
public:
  Pause(Kotonoha::Engine &engine, GameSession &session)
      : InGameMenu(engine), session_(session) {}
  bool IsOpen() const override { return session_.paused(); }
  void open() override { session_.setPaused(true); }
  void close() override { session_.setPaused(false); }
  bool onEvent(Kotonoha::EventRef event) override {
    if (IsOpen()) {
      session_.event(event);
      return true;
    }
    return false;
  }

protected:
  int itemCount() const override { return 0; }
  void renderPanel(Kotonoha::RendererRef renderer) override {
    session_.drawPause(renderer);
  }

private:
  GameSession &session_;
};

} // namespace

GameRuntime::GameRuntime(std::shared_ptr<GameAssets> assets)
    : Runtime("Doki Doki Literature Club!", "0.1.0",
              "me.hirameki.kotonoha.dokidokiliteratureclub"),
      assets_(std::move(assets)) {
  if (!assets_)
    throw std::invalid_argument(
        "Doki Doki Literature Club requires its game directory");
}

GameRuntime::~GameRuntime() { shutdown(); }

void GameRuntime::saveGame(const std::string &path) {
  if (!session_)
    throw std::logic_error("No DDLC session");
  session_->saveGame(path);
}
void GameRuntime::loadGame(const std::string &path) {
  if (!session_)
    throw std::logic_error("No DDLC session");
  session_->loadGame(path);
  if (currentState != Kotonoha::GameState::Gameplay) {
    SetState(Kotonoha::GameState::Gameplay);
    init(*engine_);
  }
}

void GameRuntime::shutdown() {
  ClearScenes();
  pause_.reset();
  choices_.reset();
  dialogue_.reset();
  menu_.reset();
  stage_.reset();
  session_.reset();
  if (assets_)
    assets_->clearCache();
  engine_ = nullptr;
}

void GameRuntime::initGlobal(Kotonoha::Engine &engine) {
  engine_ = &engine;
  if (session_)
    return;
  session_ = std::make_unique<GameSession>(engine, assets_);
  stage_ = std::make_unique<Stage>(engine, *session_);
  menu_ = std::make_unique<Frontend>(engine, *session_);
  dialogue_ = std::make_unique<Dialogue>(engine, *session_);
  choices_ = std::make_unique<Choices>(engine, *session_);
  pause_ = std::make_unique<Pause>(engine, *session_);
}

void GameRuntime::addGameScenes() {
  for (Kotonoha::Scene *scene : {stage_.get(), menu_.get(), dialogue_.get(),
                                 choices_.get(), pause_.get()})
    AddScene(scene);
}
void GameRuntime::initMenu(Kotonoha::Engine &) { addGameScenes(); }
void GameRuntime::initGameplay(Kotonoha::Engine &) {
  if (session_ && !session_->playing())
    session_->startGame();
  addGameScenes();
}
bool GameRuntime::handleBackAction() {
  return session_ && session_->backAction();
}
bool GameRuntime::handleBacklogAction() {
  return session_ && session_->openHistory();
}
int GameRuntime::translationRoute() const {
  return session_ ? session_->translationRoute() : -1;
}

void GameRuntime::update() {
  if (currentState != Kotonoha::GameState::Splash && session_ && engine_) {
    session_->update(engine_->isStoryPaused());
    const auto desired = session_->playing() ? Kotonoha::GameState::Gameplay
                                             : Kotonoha::GameState::Menu;
    if (desired != currentState) {
      SetState(desired);
      init(*engine_);
    }
  }
  Runtime::update();
}

bool GameRuntime::onEvent(Kotonoha::EventRef event) {
  auto mapped = Kotonoha::MapControllerEvent(event);
  return Runtime::onEvent(mapped);
}

void GameRuntime::render(Kotonoha::RendererRef renderer, float scale) {
  if (currentState == Kotonoha::GameState::Splash || !session_)
    return Runtime::render(renderer, scale);
  session_->beginFrame(renderer, scale);
  try {
    Runtime::render(renderer, scale);
    session_->endFrame(renderer);
  } catch (...) {
    session_->abortFrame(renderer);
    throw;
  }
}

Kotonoha::Translation::SourceTexts GameRuntime::translationSources() const {
  return assets_->script().translationSources();
}
std::string GameRuntime::debugNode() const {
  return session_ ? session_->debugNode() : std::string();
}

std::vector<Kotonoha::DebugNode> GameRuntime::debugNodes() const {
  std::vector<Kotonoha::DebugNode> result;
  for (const auto &label : assets_->script().labelNames()) {
    const auto *node = assets_->script().label(label);
    Kotonoha::DebugNode entry{
        label, node->file + ":" + std::to_string(node->line), {}};
    const auto compiled = std::filesystem::path(node->file)
                              .replace_extension(".rpyc")
                              .generic_string();
    if (assets_->contains(compiled))
      entry.assets.push_back(compiled);
    result.push_back(std::move(entry));
  }
  return result;
}

void GameRuntime::debugJumpToNode(const std::string &id) {
  if (!session_)
    throw std::logic_error("Game is not initialized");
  session_->debugJump(id);
}

} // namespace Ddlc
