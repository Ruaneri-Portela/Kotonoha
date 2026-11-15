#include "PmailComponents.hpp"
#include "PmailBacklog.hpp"
#include "PmailPauseMenu.hpp"
#include "PmailStage.hpp"
#include "PmailTextBox.hpp"
#include "PmailTitleMenu.hpp"
#include <Pmail/Port.hpp>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailComponents::PmailComponents(PmailGame &game, Kotonoha::Engine &engine,
                                 Kotonoha::Providers::SnowAssets &assets)
    : game_(game), engine_(engine), assets_(assets) {}

void PmailComponents::init() {
  stage_ = createStage();
  textBox_ = createTextBox();
  pauseMenu_ = createPauseMenu();
  backlog_ = createBacklog();
  for (Kotonoha::Scene *scene :
       {stage_.get(), static_cast<Kotonoha::Scene *>(textBox_.get()),
        static_cast<Kotonoha::Scene *>(pauseMenu_.get()),
        static_cast<Kotonoha::Scene *>(backlog_.get())})
    engine_.GetRuntime().AddScene(scene, Kotonoha::GameState::Gameplay);
  auto *box = static_cast<PmailTextBox *>(textBox_.get());
  box->setMenuAction([this] { pauseMenu_->open(); });
  box->setBacklogAction([this] { backlog_->open(); });
  static_cast<PmailPauseMenu *>(pauseMenu_.get())->setBacklogAction([this] {
    backlog_->open();
  });
}

void PmailComponents::onEvent(Kotonoha::EventRef event) {
  if (backlog_ && backlog_->isVisible()) {
    engine_.GetRuntime().dispatchEvent(*backlog_, event);
    return;
  }
  if (pauseMenu_ && pauseMenu_->IsOpen()) {
    engine_.GetRuntime().dispatchEvent(*pauseMenu_, event);
    return;
  }
  if (textBox_ && textBox_->onEvent(event))
    return;
  if (stage_)
    stage_->onEvent(event);
}

std::unique_ptr<Kotonoha::GameMenu> PmailComponents::createTitleMenu() {
  return std::make_unique<PmailTitleMenu>(engine_, game_, assets_);
}

std::unique_ptr<Kotonoha::Scene> PmailComponents::createStage() {
  return std::make_unique<PmailStage>(engine_, game_, assets_);
}

std::unique_ptr<Kotonoha::VNTextBox> PmailComponents::createTextBox() {
  return std::make_unique<PmailTextBox>(engine_, game_, assets_);
}

std::unique_ptr<Kotonoha::InGameMenu> PmailComponents::createPauseMenu() {
  return std::make_unique<PmailPauseMenu>(engine_, game_);
}

std::unique_ptr<Kotonoha::VNBacklog> PmailComponents::createBacklog() {
  return std::make_unique<PmailBacklog>(engine_, game_);
}

void PmailComponents::update(float dt) {
  if (stage_)
    stage_->update();
  if (textBox_) {
    static_cast<PmailTextBox *>(textBox_.get())
        ->setPresentationPaused(engine_.isStoryPaused());
    const auto rawMessage = game_.message();
    const auto rawSpeaker = game_.speaker();
    const auto current = game_.vm().current();
    const int route = current ? int(current->id & 0xfff) : 0;
    const auto message = engine_.GetTranslation().text(route, rawMessage);
    const auto speaker = engine_.GetTranslation().text(route, rawSpeaker);
    if (message != textBox_->getText() || speaker != textBox_->getSpeaker()) {
      textBox_->setDialogue(speaker, message);
      if (!message.empty())
        backlog_->addEntry(speaker, message);
    }
    if (message.empty())
      textBox_->hide();
    else
      textBox_->show();
    textBox_->update();
  }
  if (pauseMenu_)
    pauseMenu_->update();
  if (backlog_)
    backlog_->update();
}

void PmailComponents::render(Kotonoha::RendererRef renderer) {
  if (stage_)
    stage_->render(renderer, 1.0f);
  if (textBox_)
    textBox_->render(renderer, 1.0f);
  if (pauseMenu_)
    engine_.GetRuntime().renderScene(*pauseMenu_, renderer, 1.0f);
  if (backlog_)
    engine_.GetRuntime().renderScene(*backlog_, renderer, 1.0f);
}

} // namespace Kotonoha::PmailPort
