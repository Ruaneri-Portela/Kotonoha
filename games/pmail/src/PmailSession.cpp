#include "PmailSession.hpp"
#include "PmailComponents.hpp"
#include "PmailTextBox.hpp"
#include "PmailTitleMenu.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <iostream>

namespace Kotonoha::PmailPort {

PmailSession::PmailSession(
    std::shared_ptr<PmailGame> game, Kotonoha::Engine &engine,
    std::shared_ptr<Kotonoha::Providers::SnowAssets> assets)
    : game_(std::move(game)), engine_(engine), assets_(std::move(assets)) {
  components_ = std::make_unique<PmailComponents>(*game_, engine_, *assets_);
  titleMenu_ = std::make_unique<PmailTitleMenu>(engine_, *game_, *assets_);
}

PmailSession::~PmailSession() = default;

void PmailSession::init() {
  if (initialized_)
    return;
  components_->init();
  initialized_ = true;
}

void PmailSession::shutdown() { initialized_ = false; }

void PmailSession::setMenuActions(std::function<void()> save,
                                  std::function<void()> load,
                                  std::function<void()> title) {
  components_->pauseMenu()->SetOnSave(std::move(save));
  loadAction_ = load;
  components_->pauseMenu()->SetOnLoad(std::move(load));
  components_->pauseMenu()->SetOnReturnToMenu(std::move(title));
}

std::vector<Kotonoha::DialogueEntry> PmailSession::backlogEntries() const {
  return components_->backlog()->getEntries();
}

void PmailSession::restoreBacklog(
    const std::vector<Kotonoha::DialogueEntry> &entries) {
  components_->update(0);
  components_->textBox()->completeDialogue();
  components_->backlog()->clear();
  for (const auto &entry : entries) {
    components_->backlog()->addEntry(entry.speaker, entry.text);
    components_->backlog()->setLastVoice(entry.voice);
  }
}

bool PmailSession::paused() const {
  return engine_.isStoryPaused() ||
         (components_ && (components_->pauseMenu()->IsOpen() ||
                          components_->backlog()->isVisible()));
}

void PmailSession::closeMenus(bool clearBacklog) {
  lastUpdateMs_ = SDL_GetTicks();
  components_->pauseMenu()->close();
  components_->backlog()->close();
  if (clearBacklog)
    components_->backlog()->clear();
  if (clearBacklog) {
    auto *box = static_cast<PmailTextBox *>(components_->textBox());
    box->setAutoMode(false);
    box->setUserHidden(false);
  }
}

void PmailSession::update() {
  if (!initialized_ || !game_)
    return;

  if (game_->atTitle()) {
    updateTitle();
  } else {
    updateGameplay();
  }
}

void PmailSession::render(Kotonoha::RendererRef renderer) {
  if (!initialized_ || !game_)
    return;

  if (game_->atTitle()) {
    renderTitle(renderer);
  } else {
    renderGameplay(renderer);
  }
}

void PmailSession::onEvent(Kotonoha::EventRef event) {
  if (!initialized_ || !game_)
    return;

  if (game_->atTitle()) {
    SDL_FPoint stagePoint{0, 0};
    bool haveStagePoint = false;
    if (event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      const auto frame = Kotonoha::UI::fit({800.0f, 600.0f}, {800, 600});
      const SDL_Event mapped = Kotonoha::UI::toFrame(event, frame);
      stagePoint.x = mapped.type == SDL_EVENT_MOUSE_MOTION ? mapped.motion.x
                                                           : mapped.button.x;
      stagePoint.y = mapped.type == SDL_EVENT_MOUSE_MOTION ? mapped.motion.y
                                                           : mapped.button.y;
      haveStagePoint = true;
    }
    HandleTitleEvent(event, stagePoint, haveStagePoint);
  } else {
    if (components_ && components_->backlog()->isVisible()) {
      components_->onEvent(event);
      return;
    }
    if (components_ && components_->pauseMenu()->IsOpen()) {
      components_->onEvent(event);
      return;
    }
    if (game_->choosing()) {
      const auto choices = game_->choices();
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
          game_->choose(size_t(event.key.key - SDLK_1));
          return;
        }
        if (event.key.key == SDLK_RETURN && !choices.empty()) {
          game_->choose(0);
          return;
        }
      }
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          event.button.button == SDL_BUTTON_LEFT) {
        for (size_t i = 0; i < choices.size(); ++i) {
          const float y = 170.0f + float(i) * 52.0f;
          if (event.button.x >= 100 && event.button.x < 700 &&
              event.button.y >= y && event.button.y < y + 44) {
            game_->choose(i);
            return;
          }
        }
      }
    }
    if (components_)
      components_->onEvent(event);
  }
}

bool PmailSession::isAtTitle() const { return game_->atTitle(); }

void PmailSession::updateTitle() {
  lastUpdateMs_ = SDL_GetTicks();
  game_->pump(10000);
}

void PmailSession::updateGameplay() {
  const auto now = SDL_GetTicks();
  const auto elapsed =
      lastUpdateMs_ ? uint32_t(std::min<uint64_t>(now - lastUpdateMs_, 1000))
                    : 0;
  lastUpdateMs_ = now;
  if (components_ && !paused()) {
    game_->advanceAnimation(elapsed);
    game_->pump(10000);
  }
  if (components_) {
    components_->update(float(elapsed) / 1000.0f);
  }
}

void PmailSession::renderTitle(Kotonoha::RendererRef renderer) {
  titleMenu_->renderTitle(renderer);
  for (int i = 0; i < game_->titleCount(); ++i)
    titleMenu_->renderItem(renderer, i, i == game_->titleSelection());
}

void PmailSession::renderGameplay(Kotonoha::RendererRef renderer) {
  if (components_) {
    components_->render(renderer);
  }
  if (game_->choosing()) {
    const auto choices = game_->choices();
    for (size_t i = 0; i < choices.size(); ++i) {
      const SDL_FRect area{100.0f, 170.0f + float(i) * 52.0f, 600.0f, 44.0f};
      engine_.GetUI().button(renderer, area, choices[i]);
    }
  }
}

void PmailSession::chooseTitle(int index) {
  if (index == 1 && loadAction_)
    loadAction_();
  else
    game_->titleChoose(index);
}

bool PmailSession::HandleTitleEvent(Kotonoha::EventRef event,
                                    SDL_FPoint stagePoint,
                                    bool haveStagePoint) {
  if (event.type == SDL_EVENT_MOUSE_MOTION && haveStagePoint) {
    for (int i = 0; i < game_->titleCount(); ++i) {
      const float y = float(326 + 62 * i);
      if (stagePoint.x >= 189 && stagePoint.x < 189 + 423 &&
          stagePoint.y >= y && stagePoint.y < y + 55) {
        game_->titleHover(i);
        return true;
      }
    }
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    if (haveStagePoint) {
      for (int i = 0; i < game_->titleCount(); ++i) {
        const float y = float(326 + 62 * i);
        if (stagePoint.x >= 189 && stagePoint.x < 189 + 423 &&
            stagePoint.y >= y && stagePoint.y < y + 55) {
          chooseTitle(i);
          return true;
        }
      }
    }
    return true;
  }
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    switch (event.key.key) {
    case SDLK_UP:
      game_->titleMove(-1);
      return true;
    case SDLK_DOWN:
      game_->titleMove(1);
      return true;
    case SDLK_RETURN:
      chooseTitle(game_->titleSelection());
      return true;
    case SDLK_ESCAPE:
      return true;
    default:
      break;
    }
  }
  return false;
}

void PmailSession::handleNative(int, const std::vector<::Pmail::Arg> &) {
  // The runtime owns audio; the game host owns script and title commands.
}

} // namespace Kotonoha::PmailPort
