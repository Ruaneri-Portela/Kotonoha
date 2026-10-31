#include "ZeroComponents.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>

#include <algorithm>

namespace ZeroNoTsukaima {
namespace {

void DrawCentered(SDL_Renderer *r, Picture *p, const SDL_FRect &box, float dy = 0) {
  if (!p || !*p) return;
  const SDL_FRect dst{box.x + (box.w - p->Width()) / 2, box.y + (box.h - p->Height()) / 2 + dy, p->Width(),
                      p->Height()};
  p->Draw(r, nullptr, &dst);
}

bool IsKey(Kotonoha::EventRef e, SDL_Keycode key) { return e.type == SDL_EVENT_KEY_DOWN && e.key.key == key; }

bool WheelUp(Kotonoha::EventRef e) {
  if (e.type != SDL_EVENT_MOUSE_WHEEL) return false;
  const float y = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
  return y > 0;
}

bool RightClick(Kotonoha::EventRef e) {
  return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT;
}

} // namespace

// ---------------------------------------------------------------- title

ZeroTitleMenu::ZeroTitleMenu(Kotonoha::Engine &engine, ZeroSession &session) : GameMenu(engine), session_(session) {}

bool ZeroTitleMenu::isVisible() const { return !session_.Playing(); }

void ZeroTitleMenu::onEvent(Kotonoha::EventRef event) {
  if (isVisible()) session_.FrontendEvent(event);
}

void ZeroTitleMenu::update() {
  if (!isVisible()) return;
  switch (session_.FrontendTick()) {
  case ZeroSession::TitleAction::NewGame: activate(0); break;
  case ZeroSession::TitleAction::Load: activate(1); break;
  case ZeroSession::TitleAction::None: break;
  }
}

void ZeroTitleMenu::activate(int index) {
  // New game and load both start the story, then GameMenu switches to Gameplay.
  if (index == 0 || index == 1) {
    if (session_.Start()) GameMenu::activate(0);
    return;
  }
  GameMenu::activate(index);
}

void ZeroTitleMenu::renderTitle(Kotonoha::RendererRef renderer) { session_.DrawFrontend(&renderer); }

// ---------------------------------------------------------------- stage

void ZeroStage::render(Kotonoha::RendererRef renderer, float) { session_.DrawStage(&renderer); }

// ---------------------------------------------------------------- text box

ZeroTextBox::ZeroTextBox(Kotonoha::Engine &engine, ZeroSession &session)
    : VNTextBox(engine, "", ""), session_(session) {
  hide();
}

double ZeroTextBox::charactersPerSecond() const {
  const int delay = session_.TextDelayMs();
  return delay > 0 ? 1000.0 / delay : 1e9;
}

// Follows the script page; a page that only grew keeps the part already shown.
void ZeroTextBox::SyncPage() {
  if (!session_.HasMessage()) {
    hide();
    pageKey_.clear();
    return;
  }
  show();
  const Game &game = *session_.Story();
  const std::string key = game.Speaker() + "\n" + game.Text();
  if (key == pageKey_) return;
  const bool grew = !pageKey_.empty() && key.compare(0, pageKey_.size(), pageKey_) == 0;
  const double shown = grew ? visibleChars : 0.0;
  pageKey_ = key;
  setDialogue(game.Speaker(), game.Text());
  if (grew) {
    visibleChars = shown;
    visibleDialogue = fullDialogue.substr(0, Kotonoha::utf8Prefix(fullDialogue, std::size_t(shown)));
    isComplete = visibleDialogue.size() == fullDialogue.size();
  }
  if (session_.TextDelayMs() <= 0) completeDialogue();
  doneAt_ = SDL_GetTicks();
}

// Typewriter, then auto mode and Ctrl skip (GameplayFlow pace of the original port).
void ZeroTextBox::update() {
  SyncPage();
  VNTextBox::update();
  const Uint64 now = SDL_GetTicks();
  if (!session_.Playing()) return;
  const bool idle = !session_.ScreenActive() && !session_.Choosing() && !engine.isStoryPaused();
  if (!idle) {
    doneAt_ = now;
    return;
  }
  if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0 && session_.CanSkip()) {
    completeDialogue();
    if (session_.WaitingForClick()) session_.Advance(-1);
    else session_.Skip();
    return;
  }
  if (session_.VoiceActive() || (isVisible() && !isComplete)) doneAt_ = now;
  if (session_.Auto() && session_.WaitingForClick() && now - doneAt_ >= Uint64(session_.AutoDelayMs())) {
    doneAt_ = now;
    session_.Advance(-1);
  }
}

// The first click completes a page that is still being revealed.
void ZeroTextBox::Click() {
  if (isVisible() && !isComplete) {
    completeDialogue();
    doneAt_ = SDL_GetTicks();
    return;
  }
  session_.Advance(-1);
}

bool ZeroTextBox::handleEvent(Kotonoha::EventRef event) {
  if (!session_.Playing()) return false;
  if (IsKey(event, SDLK_PAGEUP) || WheelUp(event)) {
    openBacklog();
    return true;
  }
  if (RightClick(event) || IsKey(event, SDLK_AC_BACK)) {
    openMenu();
    return true;
  }
  if (session_.Choosing()) return false;
  if (IsKey(event, SDLK_A) && !event.key.repeat) {
    session_.SetAuto(!session_.Auto());
    doneAt_ = SDL_GetTicks();
    return true;
  }
  const bool advance = IsKey(event, SDLK_RETURN) || IsKey(event, SDLK_KP_ENTER) || IsKey(event, SDLK_SPACE) ||
                       IsKey(event, SDLK_DOWN) || IsKey(event, SDLK_Z) ||
                       (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button != SDL_BUTTON_RIGHT);
  if (!advance) return false;
  // Any click stops auto mode (KAG cancelAuto).
  if (session_.Auto()) session_.SetAuto(false);
  else Click();
  return true;
}

void ZeroTextBox::renderFrame(Kotonoha::RendererRef renderer) { session_.DrawMessageWindow(&renderer); }

void ZeroTextBox::renderDialogue(Kotonoha::RendererRef renderer) {
  session_.DrawMessageText(&renderer, visibleDialogue);
}

void ZeroTextBox::renderIndicator(Kotonoha::RendererRef renderer) { session_.DrawMessageCursor(&renderer); }

// ---------------------------------------------------------------- choices

ZeroChoice::ZeroChoice(Kotonoha::Engine &engine, ZeroSession &session)
    : ChoiceQuestionScene(engine, "", {}, nullptr), session_(session) {}

void ZeroChoice::update() {
  const bool want = session_.Choosing() && Wanted();
  if (!want) {
    if (active) hide();
    shown_.clear();
    return;
  }
  const auto labels = Labels();
  if (labels.empty() || (active && labels == shown_)) return;
  std::vector<Kotonoha::ChoiceOption> choices;
  for (std::size_t i = 0; i < labels.size(); ++i) choices.push_back({labels[i], int(i)});
  shown_ = labels;
  setQuestion("", choices, [this](int id, const std::string &) { session_.Advance(id); });
  focus_ = InitialFocus();
  selected = std::size_t(std::max(focus_, 0));
  for (const auto &label : labels) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[choice] %s", label.c_str());
}

void ZeroChoice::Focus(int index) {
  if (index == focus_ || index < 0 || index >= int(options.size())) return;
  focus_ = index;
  selected = std::size_t(index);
  session_.SystemSe("SYS_SE01");
}

void ZeroChoice::select(std::size_t index) {
  session_.SystemSe("SYS_SE02");
  focus_ = InitialFocus();
  ChoiceQuestionScene::select(index);
}

// No initial focus on the select window: the first UP picks the last button,
// the first DOWN the first one. Circle = Enter / Space / Z.
void ZeroChoice::onEvent(Kotonoha::EventRef event) {
  if (!active || options.empty()) return;
  layout();
  const int n = int(options.size());
  auto hit = [&](float x, float y) {
    const SDL_FPoint pt{x, y};
    for (int i = 0; i < n; ++i)
      if (SDL_PointInRectFloat(&pt, &options[std::size_t(i)].hitBox)) return i;
    return -1;
  };
  if (event.type == SDL_EVENT_KEY_DOWN) {
    switch (event.key.key) {
    case SDLK_ESCAPE:
    case SDLK_AC_BACK: openMenu(); break;
    case SDLK_B:
    case SDLK_PAGEUP: openBacklog(); break;
    case SDLK_UP: Focus(focus_ < 0 ? n - 1 : (focus_ + n - 1) % n); break;
    case SDLK_DOWN: Focus(focus_ < 0 ? 0 : (focus_ + 1) % n); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
    case SDLK_Z:
      if (focus_ >= 0) select(std::size_t(focus_));
      break;
    default: break;
    }
  } else if (WheelUp(event)) {
    openBacklog();
  } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
    Focus(hit(event.motion.x, event.motion.y));
  } else if (RightClick(event)) {
    openMenu();
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
    if (const int i = hit(event.button.x, event.button.y); i >= 0) select(std::size_t(i));
  }
}

// SelectWindow (ENVINIT selectopt): main_select-off / -on, 400x50, centred;
// button i at top = area.h / (n + 1) * (i + 1) - 25 inside the selopt area.
bool ZeroSelectChoice::Wanted() const { return !session_.Story()->Moving() && !session_.Story()->Roulette(); }

std::vector<std::string> ZeroSelectChoice::Labels() const {
  std::vector<std::string> labels = session_.Story()->Choices();
  for (auto &label : labels) label = session_.Translate(label);
  return labels;
}

void ZeroSelectChoice::layout() {
  constexpr int kSelectW = 400, kSelectH = 50;
  const SDL_Rect area = session_.Story()->SelectArea();
  const int n = int(options.size());
  const int h = area.h / (n + 1);
  for (int i = 0; i < n; ++i)
    options[std::size_t(i)].hitBox = {float(kGameWidth / 2 - kSelectW / 2), float(area.y + h * (i + 1) - kSelectH / 2),
                                      float(kSelectW), float(kSelectH)};
}

void ZeroSelectChoice::renderOption(Kotonoha::RendererRef renderer, std::size_t index, bool focused) {
  const auto &option = options[index];
  const SDL_FRect &b = option.hitBox;
  session_.DrawNormal(&renderer, focused && focus_ >= 0 ? "MAIN_SELECT-ON" : "MAIN_SELECT-OFF", b.x, b.y);
  const SDL_Color black{0, 0, 0, 255};
  const SDL_FPoint size = session_.DrawText(&renderer, option.label, 0, 0, 24, black, true);
  session_.DrawText(&renderer, option.label, b.x + (b.w - size.x) / 2, b.y + (b.h - size.y) / 2, 24, black);
}

// MoveState (map): MOVE_MOVE_OFF / _ON at (39, 70 + 60n), 247x48, with the
// destination name picture; the focused one shows its pose at SCWIDTH / 2 + 132.
bool ZeroMoveChoice::Wanted() const { return session_.Story()->Moving(); }

std::vector<std::string> ZeroMoveChoice::Labels() const {
  std::vector<std::string> labels;
  for (const auto &dest : session_.Story()->MoveDestinations()) labels.push_back(dest.text);
  return labels;
}

void ZeroMoveChoice::layout() {
  for (std::size_t i = 0; i < options.size(); ++i) options[i].hitBox = {39, 70 + 60 * float(i), 247, 48};
}

void ZeroMoveChoice::renderPanel(Kotonoha::RendererRef renderer) {
  const Game &game = *session_.Story();
  const auto &catalog = session_.Disc().Source().Catalog();
  const int back = catalog.ResolveStage(game.MoveBack());
  if (Picture *p = back >= 0 ? session_.SceneIndexPicture(back) : session_.NamedScene(game.MoveBack())) {
    const SDL_FRect dst{0, 0, p->Width(), p->Height()};
    p->Draw(&renderer, nullptr, &dst);
  }
  const auto &dest = game.MoveDestinations();
  if (focus_ >= 0 && focus_ < int(dest.size()) && !dest[std::size_t(focus_)].pose.empty())
    if (Picture *pose = session_.SceneIndexPicture(catalog.ResolveSprite(dest[std::size_t(focus_)].pose))) {
      const SDL_FRect dst{float(kGameWidth / 2 + 132 - int(pose->Width()) / 2), 0, pose->Width(), pose->Height()};
      pose->Draw(&renderer, nullptr, &dst);
    }
}

void ZeroMoveChoice::renderOption(Kotonoha::RendererRef renderer, std::size_t index, bool focused) {
  const auto &option = options[index];
  const bool on = focused && focus_ >= 0;
  session_.DrawNormal(&renderer, on ? "MOVE_MOVE_ON" : "MOVE_MOVE_OFF", option.hitBox.x, option.hitBox.y);
  char name[48];
  SDL_snprintf(name, sizeof name, "MOVETEXT_POINT%02d_%s", ZeroSession::MovePosition(option.label), on ? "ON" : "OFF");
  DrawCentered(&renderer, session_.NormalPicture(name), option.hitBox, -2);
}

// ---------------------------------------------------------------- in-game screens

ZeroCommandMenu::ZeroCommandMenu(Kotonoha::Engine &engine, ZeroSession &session)
    : InGameMenu(engine), session_(session) {}

void ZeroCommandMenu::onEvent(Kotonoha::EventRef event) {
  if (IsOpen()) session_.ScreenEvent(event);
  else if (session_.Playing()) InGameMenu::onEvent(event); // Esc opens CommandState
}

// "Title" in CommandState and the end of the story leave through InGameMenu.
void ZeroCommandMenu::update() {
  if (session_.TakeTitleRequest() || session_.Ended()) activate(5);
}

void ZeroBacklog::onEvent(Kotonoha::EventRef event) {
  if (isVisible()) session_.ScreenEvent(event);
  else if (session_.Playing() && !session_.ScreenActive()) VNBacklog::onEvent(event); // B opens HistoryState
}

} // namespace ZeroNoTsukaima
