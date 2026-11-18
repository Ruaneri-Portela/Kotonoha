#include "PmailSession.hpp"
#include "PmailComponents.hpp"
#include "PmailExtrasMenu.hpp"
#include "PmailExtrasViewer.hpp"
#include "PmailTextBox.hpp"
#include "PmailTitleMenu.hpp"
#include <Kotonoha/Filesystem/Filesystem.hpp>
#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Pmail/Port.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace Kotonoha::PmailPort {

PmailSession::PmailSession(
    std::shared_ptr<PmailGame> game, Kotonoha::Engine &engine,
    std::shared_ptr<Kotonoha::Providers::SnowAssets> assets)
    : game_(std::move(game)), engine_(engine), assets_(std::move(assets)) {
  components_ = std::make_unique<PmailComponents>(*game_, engine_, *assets_);
  titleMenu_ = std::make_unique<PmailTitleMenu>(engine_, *game_, *assets_);
  extrasMenu_ = std::make_unique<PmailExtrasMenu>(engine_, *game_, *assets_);
  extrasViewer_ =
      std::make_unique<PmailExtrasViewer>(engine_, *game_, *assets_);
  game_->onChoiceSelected = [this](const std::string &selected) {
    if (components_ && components_->backlog() && !selected.empty())
      components_->backlog()->addEntry("", selected);
  };
  loadNameProfile();
  loadExtrasProfile();
  extrasVersion_ = game_->extrasVersion();
}

PmailSession::~PmailSession() {
  if (nameEntryOpen_)
    SDL_StopTextInput(&engine_.GetWindow());
}

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
  if (nameEntryOpen_)
    endNameEntry(false);
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

  if (game_->extrasVersion() != extrasVersion_) {
    saveExtrasProfile();
    extrasVersion_ = game_->extrasVersion();
  }

  if (game_->extrasMenuActive()) {
    // The native omake menu owns the whole 800x600 stage while it is open.
    lastUpdateMs_ = SDL_GetTicks();
    return;
  }

  if (game_->extrasViewerActive()) {
    // The native CG / recollection / music viewers are modal too.
    lastUpdateMs_ = SDL_GetTicks();
    return;
  }

  if (game_->atTitle()) {
    updateTitle();
  } else {
    updateGameplay();
  }
}

void PmailSession::render(Kotonoha::RendererRef renderer) {
  if (!initialized_ || !game_)
    return;

  if (game_->extrasMenuActive()) {
    if (extrasMenu_)
      extrasMenu_->render(renderer);
    return;
  }

  if (game_->extrasViewerActive()) {
    if (extrasViewer_)
      extrasViewer_->render(renderer);
    return;
  }

  if (game_->atTitle()) {
    renderTitle(renderer);
  } else {
    renderGameplay(renderer);
  }
}

void PmailSession::onEvent(Kotonoha::EventRef event) {
  if (!initialized_ || !game_)
    return;
  if (nameEntryOpen_) {
    handleNameEntry(event);
    return;
  }

  if (game_->extrasMenuActive()) {
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
    handleExtrasEvent(event, stagePoint, haveStagePoint);
    return;
  }

  if (game_->extrasViewerActive()) {
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
    handleExtrasViewerEvent(event, stagePoint, haveStagePoint);
    return;
  }

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
    if (game_->staffRollActive()) {
      const bool skip =
          (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
           event.button.button == SDL_BUTTON_LEFT) ||
          (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
           (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER ||
            event.key.key == SDLK_SPACE || event.key.key == SDLK_ESCAPE));
      if (skip) {
        game_->skipStaffRoll();
        return;
      }
    }
    if (game_->nativeTimeWaitActive()) {
      // pmail.exe:0x423a00 polls the input ring every iteration and exits the
      // pause as soon as an advance event arrives. The text box is hidden for
      // these timer-only beats, so the session owns the skip.
      const bool advance =
          (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
           event.button.button == SDL_BUTTON_LEFT) ||
          (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
           (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE));
      if (advance) {
        game_->click();
        return;
      }
    }
    if (game_->choosing()) {
      const auto choices = game_->choices();
      if (!choices.empty()) {
        choiceSelection_ = std::min(choiceSelection_, choices.size() - 1);
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
          if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
            const auto index = static_cast<std::size_t>(event.key.key - SDLK_1);
            if (index < choices.size()) {
              choiceSelection_ = 0;
              game_->choose(index);
            }
            return;
          }
          if (event.key.key == SDLK_UP || event.key.key == SDLK_LEFT) {
            choiceSelection_ =
                (choiceSelection_ + choices.size() - 1) % choices.size();
            return;
          }
          if (event.key.key == SDLK_DOWN || event.key.key == SDLK_RIGHT) {
            choiceSelection_ = (choiceSelection_ + 1) % choices.size();
            return;
          }
          if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            const auto index = choiceSelection_;
            choiceSelection_ = 0;
            game_->choose(index);
            return;
          }
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION ||
            (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             event.button.button == SDL_BUTTON_LEFT)) {
          const float x = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x
                                                               : event.button.x;
          const float y = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y
                                                               : event.button.y;
          for (std::size_t i = 0; i < choices.size(); ++i) {
            const float top = 170.0f + static_cast<float>(i) * 52.0f;
            if (x >= 100.0f && x < 700.0f && y >= top && y < top + 44.0f) {
              choiceSelection_ = i;
              if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                choiceSelection_ = 0;
                game_->choose(i);
              }
              return;
            }
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
  if (nameEntryOpen_)
    drawNameEntry(renderer);
}

void PmailSession::renderGameplay(Kotonoha::RendererRef renderer) {
  if (components_) {
    components_->render(renderer);
  }
  if (game_->choosing()) {
    const auto choices = game_->choices();
    for (size_t i = 0; i < choices.size(); ++i) {
      const SDL_FRect area{100.0f, 170.0f + float(i) * 52.0f, 600.0f, 44.0f};
      engine_.GetUI().button(
          renderer, area, choices[i],
          i == std::min(choiceSelection_, choices.size() - 1));
    }
  }
}

void PmailSession::chooseTitle(int index) {
  if (index == 0) {
    beginNameEntry();
  } else if (index == 1 && loadAction_) {
    loadAction_();
  } else {
    game_->titleChoose(index);
  }
}

namespace {
std::filesystem::path nameProfilePath() {
  return Kotonoha::saveDirectory() / "pmail-names.txt";
}
std::filesystem::path extrasProfilePath() {
  // Original SAVE\SAVEFL99.SAV stores the 4096-dword global flag block used
  // by SYS 215 (pmail.exe:0x40eea0/0x40eef0). Keep the same unlock set in a
  // small, human-readable profile next to the player-name profile.
  return Kotonoha::saveDirectory() / "pmail-extras.txt";
}
} // namespace

void PmailSession::loadNameProfile() {
  try {
    const auto path = nameProfilePath();
    if (!std::filesystem::exists(path))
      return;
    const auto bytes = Kotonoha::Filesystem::readLocalFile(path, 1024);
    std::istringstream file(std::string(bytes.begin(), bytes.end()));
    std::string no, ho;
    if (std::getline(file, no) && std::getline(file, ho))
      game_->setPlayerNames(no, ho);
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PmailPort] Player name profile ignored: %s", error.what());
  }
}

void PmailSession::saveNameProfile() const {
  try {
    const auto path = nameProfilePath();
    std::filesystem::create_directories(path.parent_path());
    Kotonoha::writeTextFileAtomically(path.string(),
                                      game_->playerNameNO() + "\n" +
                                          game_->playerNameHO() + "\n");
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PmailPort] Cannot save player name profile: %s",
                error.what());
  }
}

void PmailSession::loadExtrasProfile() {
  try {
    const auto path = extrasProfilePath();
    if (!std::filesystem::exists(path))
      return;
    const auto bytes = Kotonoha::Filesystem::readLocalFile(path, 4096);
    std::istringstream file(std::string(bytes.begin(), bytes.end()));
    std::vector<std::string> names;
    std::string name;
    while (std::getline(file, name)) {
      if (!name.empty() && name.back() == '\r')
        name.pop_back();
      if (!name.empty())
        names.push_back(name);
    }
    game_->loadUnlockedExtras(names);
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PmailPort] Extra unlock profile ignored: %s", error.what());
  }
}

void PmailSession::saveExtrasProfile() const {
  try {
    const auto path = extrasProfilePath();
    std::filesystem::create_directories(path.parent_path());
    std::string text;
    for (const auto &name : game_->unlockedExtras()) {
      text += name;
      text += '\n';
    }
    Kotonoha::writeTextFileAtomically(path.string(), text);
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PmailPort] Cannot save extra unlock profile: %s",
                error.what());
  }
}

void PmailSession::beginNameEntry() {
  editableNameNO_ = game_->playerNameNO();
  editableNameHO_ = game_->playerNameHO();
  nameField_ = 0;
  nameEntryError_.clear();
  nameEntryOpen_ = true;
  if (!SDL_StartTextInput(&engine_.GetWindow()))
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PmailPort] Text input unavailable: %s", SDL_GetError());
}

void PmailSession::endNameEntry(bool accept) {
  if (!nameEntryOpen_)
    return;
  if (accept) {
    try {
      game_->setPlayerNames(editableNameNO_, editableNameHO_);
    } catch (const std::exception &error) {
      nameEntryError_ = error.what();
      return;
    }
  }
  nameEntryOpen_ = false;
  SDL_StopTextInput(&engine_.GetWindow());
  if (accept) {
    saveNameProfile();
    game_->titleChoose(0);
  }
}

bool PmailSession::handleNameEntry(Kotonoha::EventRef event) {
  if (event.type == SDL_EVENT_TEXT_INPUT) {
    auto &field = nameField_ == 0 ? editableNameNO_ : editableNameHO_;
    const std::string entered = event.text.text;
    if (entered.find_first_of("\r\n\t") == std::string::npos &&
        field.size() + entered.size() <= 64)
      field += entered;
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    if (Kotonoha::UI::clicked(event, {170, 254, 465, 46}))
      nameField_ = 0;
    else if (Kotonoha::UI::clicked(event, {170, 333, 465, 46}))
      nameField_ = 1;
    else if (Kotonoha::UI::clicked(event, {360, 420, 235, 50}))
      endNameEntry(true);
    return true;
  }
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
    return true;
  if (event.key.key == SDLK_ESCAPE) {
    endNameEntry(false);
  } else if (event.key.key == SDLK_TAB) {
    nameField_ = 1 - nameField_;
  } else if (event.key.key == SDLK_BACKSPACE) {
    auto &field = nameField_ == 0 ? editableNameNO_ : editableNameHO_;
    if (!field.empty())
      Kotonoha::eraseLastCharacter(field);
  } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
    if (nameField_ == 0)
      nameField_ = 1;
    else
      endNameEntry(true);
  }
  return true;
}

void PmailSession::drawNameEntry(Kotonoha::RendererRef renderer) {
  auto &ui = engine_.GetUI();
  ui.backdrop(renderer, {0, 0, 0, 210});
  ui.panel(renderer, {100, 143, 600, 365});
  ui.text(renderer, "Pure Mail - name customization", 150, 174, 540,
          {255, 215, 110, 255});
  ui.text(renderer, "First name ($NO)", 170, 225, 450);
  ui.button(renderer, {170, 254, 465, 46},
            editableNameNO_ + (nameField_ == 0 ? " |" : ""), nameField_ == 0);
  ui.text(renderer, "Second name ($HO)", 170, 305, 450);
  ui.button(renderer, {170, 333, 465, 46},
            editableNameHO_ + (nameField_ == 1 ? " |" : ""), nameField_ == 1);
  ui.button(renderer, {360, 420, 235, 50}, "Start game");
  ui.text(renderer, "Tab: next field | Enter: confirm | Esc: cancel", 145, 480,
          520);
  if (!nameEntryError_.empty())
    ui.text(renderer, nameEntryError_, 165, 390, 460, {250, 120, 120, 255});
}

bool PmailSession::handleExtrasEvent(Kotonoha::EventRef event,
                                     SDL_FPoint stagePoint,
                                     bool haveStagePoint) {
  if (event.type == SDL_EVENT_MOUSE_MOTION && haveStagePoint) {
    const int hover =
        extrasMenu_ ? extrasMenu_->hitTest(stagePoint.x, stagePoint.y) : 0;
    game_->extrasHover(hover);
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    if (haveStagePoint && extrasMenu_) {
      const int choice = extrasMenu_->hitTest(stagePoint.x, stagePoint.y);
      if (choice >= 1 && choice <= 4)
        game_->extrasChoose(choice);
    }
    return true;
  }
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    switch (event.key.key) {
    case SDLK_UP:
      game_->extrasMove(-1);
      return true;
    case SDLK_DOWN:
      game_->extrasMove(1);
      return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      game_->extrasChoose(game_->extrasSelection());
      return true;
    case SDLK_ESCAPE:
      // 0x41eca1: Escape takes the EXIT item (4).
      game_->extrasChoose(4);
      return true;
    default:
      break;
    }
  }
  return true;
}

bool PmailSession::handleExtrasViewerEvent(Kotonoha::EventRef event,
                                           SDL_FPoint stagePoint,
                                           bool haveStagePoint) {
  using HitKind = PmailExtrasViewer::HitKind;
  if (!extrasViewer_)
    return true;
  if (game_->extrasViewerFullView()) {
    if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
         (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN ||
          event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_SPACE)) ||
        (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
         event.button.button == SDL_BUTTON_LEFT))
      game_->extrasViewerBack();
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_MOTION && haveStagePoint) {
    const auto hit = extrasViewer_->hitTest(stagePoint.x, stagePoint.y);
    if (hit.kind == HitKind::Cell)
      game_->extrasViewerHover(hit.value);
    else if (hit.kind == HitKind::Track)
      game_->extrasViewerSetTrack(hit.value);
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    if (haveStagePoint) {
      const auto hit = extrasViewer_->hitTest(stagePoint.x, stagePoint.y);
      switch (hit.kind) {
      case HitKind::Cell:
        game_->extrasViewerHover(hit.value);
        game_->extrasViewerActivate();
        break;
      case HitKind::Category:
        game_->extrasViewerSetCategory(hit.value);
        break;
      case HitKind::PrevPage:
        game_->extrasViewerPageBy(-1);
        break;
      case HitKind::NextPage:
        game_->extrasViewerPageBy(1);
        break;
      case HitKind::Exit:
        game_->extrasViewerBack();
        break;
      case HitKind::Track:
        game_->extrasViewerSetTrack(hit.value);
        game_->extrasViewerActivate();
        break;
      case HitKind::PrevTrack:
        game_->extrasViewerTrackBy(-1);
        break;
      case HitKind::NextTrack:
        game_->extrasViewerTrackBy(1);
        break;
      case HitKind::Play:
        game_->extrasViewerActivate();
        break;
      case HitKind::Stop:
        game_->extrasViewerStopMusic();
        break;
      case HitKind::Repeat:
        game_->extrasViewerToggleRepeat();
        break;
      default:
        break;
      }
    }
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    if (game_->extrasViewerMode() == PmailGame::ExtrasViewerMode::Music)
      game_->extrasViewerTrackBy(event.wheel.y > 0 ? -1 : 1);
    else
      game_->extrasViewerPageBy(event.wheel.y > 0 ? -1 : 1);
    return true;
  }
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    const bool music =
        game_->extrasViewerMode() == PmailGame::ExtrasViewerMode::Music;
    switch (event.key.key) {
    case SDLK_ESCAPE:
      game_->extrasViewerBack();
      return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
      game_->extrasViewerActivate();
      return true;
    case SDLK_UP:
      music ? game_->extrasViewerTrackBy(-1) : game_->extrasViewerMove(0, -1);
      return true;
    case SDLK_DOWN:
      music ? game_->extrasViewerTrackBy(1) : game_->extrasViewerMove(0, 1);
      return true;
    case SDLK_LEFT:
      music ? game_->extrasViewerTrackBy(-1) : game_->extrasViewerMove(-1, 0);
      return true;
    case SDLK_RIGHT:
      music ? game_->extrasViewerTrackBy(1) : game_->extrasViewerMove(1, 0);
      return true;
    case SDLK_PAGEUP:
      game_->extrasViewerPageBy(-1);
      return true;
    case SDLK_PAGEDOWN:
      game_->extrasViewerPageBy(1);
      return true;
    case SDLK_HOME:
      if (music)
        game_->extrasViewerSetTrack(1);
      else
        game_->extrasViewerPageBy(-game_->extrasViewerPage());
      return true;
    case SDLK_END:
      if (music)
        game_->extrasViewerSetTrack(game_->extrasViewerTrackCount());
      else
        game_->extrasViewerPageBy(game_->extrasViewerPageCount() - 1 -
                                  game_->extrasViewerPage());
      return true;
    case SDLK_TAB:
      if (!music)
        game_->extrasViewerCategoryBy((event.key.mod & SDL_KMOD_SHIFT) ? -1
                                                                       : 1);
      return true;
    case SDLK_R:
      if (music)
        game_->extrasViewerToggleRepeat();
      return true;
    default:
      break;
    }
  }
  return true;
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
