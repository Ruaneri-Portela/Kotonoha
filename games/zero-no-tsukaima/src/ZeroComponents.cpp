#include "ZeroComponents.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/BackAction.hpp>

#include <algorithm>

namespace ZeroNoTsukaima {
namespace {

void DrawCentered(SDL_Renderer *r, Picture *p, const SDL_FRect &box,
                  float dy = 0) {
  if (!p || !*p)
    return;
  const SDL_FRect dst{box.x + (box.w - p->Width()) / 2,
                      box.y + (box.h - p->Height()) / 2 + dy, p->Width(),
                      p->Height()};
  p->Draw(r, nullptr, &dst);
}

bool IsKey(Kotonoha::EventRef e, SDL_Keycode key) {
  return e.type == SDL_EVENT_KEY_DOWN && e.key.key == key;
}

bool WheelUp(Kotonoha::EventRef e) {
  if (e.type != SDL_EVENT_MOUSE_WHEEL)
    return false;
  const float y =
      e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
  return y > 0;
}

bool RightClick(Kotonoha::EventRef e) {
  return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
         e.button.button == SDL_BUTTON_RIGHT;
}

SDL_Event NativeNavigation(Kotonoha::Engine &engine, Kotonoha::EventRef event) {
  SDL_Event native = event;
  if (Kotonoha::BackAction(engine, event)) {
    native = {};
    native.type = SDL_EVENT_KEY_DOWN;
    native.key.key = SDLK_ESCAPE;
  }
  return native;
}

} // namespace

// ---------------------------------------------------------------- title
ZeroTitleMenu::ZeroTitleMenu(Kotonoha::Engine &engine, ZeroSession &session)
    : GameMenu(engine), session_(session) {}

bool ZeroTitleMenu::isVisible() const { return !session_.Playing(); }

bool ZeroTitleMenu::onEvent(Kotonoha::EventRef event) {
  if (isVisible()) {
    session_.FrontendEvent(NativeNavigation(engine, event));
    return true;
  }
  return false;
}

void ZeroTitleMenu::update() {
  if (!isVisible())
    return;
  switch (session_.FrontendTick()) {
  case ZeroSession::TitleAction::NewGame:
    activate(0);
    break;
  case ZeroSession::TitleAction::Load:
    activate(1);
    break;
  case ZeroSession::TitleAction::None:
    break;
  }
}

void ZeroTitleMenu::activate(int index) {
  // New game and load both start the story, then GameMenu switches to Gameplay.
  if (index == 0 || index == 1) {
    if (session_.Start())
      GameMenu::activate(0);
    return;
  }
  GameMenu::activate(index);
}

void ZeroTitleMenu::renderTitle(Kotonoha::RendererRef renderer) {
  session_.DrawFrontend(&renderer);
}

// ---------------------------------------------------------------- stage
void ZeroStage::render(Kotonoha::RendererRef renderer, float) {
  session_.DrawStage(&renderer);
}

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
  if (key == pageKey_)
    return;
  const bool grew =
      !pageKey_.empty() && key.compare(0, pageKey_.size(), pageKey_) == 0;
  const double shown = grew ? visibleChars : 0.0;
  pageKey_ = key;
  setDialogue(game.Speaker(), game.Text());
  if (grew) {
    visibleChars = shown;
    visibleDialogue = fullDialogue.substr(
        0, Kotonoha::utf8Prefix(fullDialogue, std::size_t(shown)));
    isComplete = visibleDialogue.size() == fullDialogue.size();
  }
  if (session_.TextDelayMs() <= 0)
    completeDialogue();
  doneAt_ = SDL_GetTicks();
}

// Typewriter, then auto mode and Ctrl skip (GameplayFlow pace of the original
// port).
void ZeroTextBox::update() {
  SyncPage();
  VNTextBox::update();
  const Uint64 now = SDL_GetTicks();
  if (!session_.Playing())
    return;
  const bool idle = !session_.ScreenActive() && !session_.Choosing() &&
                    !session_.RouletteActive() && !engine.isStoryPaused();
  if (!idle) {
    cancelHeldSkip();
    doneAt_ = now;
    return;
  }
  if (skipToggle_ && !session_.CanSkip())
    skipToggle_ = false;
  if ((skipToggle_ || skipHeld_ || (SDL_GetModState() & SDL_KMOD_CTRL) != 0) &&
      session_.CanSkip()) {
    completeDialogue();
    if (session_.WaitingForClick())
      session_.Advance(-1);
    else
      session_.Skip();
    return;
  }
  if (session_.VoiceActive() || (isVisible() && !isComplete))
    doneAt_ = now;
  if (session_.Auto() && session_.WaitingForClick() &&
      now - doneAt_ >= Uint64(session_.AutoDelayMs())) {
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
  if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
      event.type == SDL_EVENT_GAMEPAD_REMOVED) {
    cancelHeldSkip();
    return false;
  }
  if (event.type == SDL_EVENT_KEY_UP &&
      (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL)) {
    cancelHeldSkip();
    return true;
  }
  if (!session_.Playing())
    return false;
  if ((IsKey(event, SDLK_LCTRL) || IsKey(event, SDLK_RCTRL)) &&
      !session_.Choosing() && !session_.ScreenActive() &&
      !session_.RouletteActive()) {
    skipHeld_ = true;
    session_.SetAuto(false);
    return true;
  }
  if (IsKey(event, SDLK_PAGEUP) || WheelUp(event)) {
    openBacklog();
    return true;
  }
  if (RightClick(event) || IsKey(event, SDLK_AC_BACK)) {
    openMenu();
    return true;
  }
  if (session_.Choosing())
    return false;
  if ((IsKey(event, SDLK_MINUS) || IsKey(event, SDLK_PLUS) ||
       IsKey(event, SDLK_EQUALS)) &&
      !event.key.repeat) {
    auto &speed = session_.System().charSpeed;
    const int next =
        std::clamp(speed + (event.key.key == SDLK_MINUS ? -1 : 1), 0, 10);
    if (next != speed) {
      speed = next;
      session_.SystemChanged();
    }
    return true;
  }
  if (IsKey(event, SDLK_S) && !event.key.repeat) {
    skipToggle_ = !skipToggle_ && session_.CanSkip();
    session_.SetAuto(false);
    return true;
  }
  if (IsKey(event, SDLK_A) && !event.key.repeat) {
    cancelHeldSkip();
    session_.SetAuto(!session_.Auto());
    doneAt_ = SDL_GetTicks();
    return true;
  }
  const bool advance = IsKey(event, SDLK_RETURN) ||
                       IsKey(event, SDLK_KP_ENTER) ||
                       IsKey(event, SDLK_SPACE) || IsKey(event, SDLK_DOWN) ||
                       IsKey(event, SDLK_Z) ||
                       (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                        event.button.button != SDL_BUTTON_RIGHT);
  if (!advance)
    return false;
  cancelHeldSkip();
  // Any click stops auto mode (KAG cancelAuto).
  if (session_.Auto())
    session_.SetAuto(false);
  else
    Click();
  return true;
}

void ZeroTextBox::renderFrame(Kotonoha::RendererRef renderer) {
  session_.DrawMessageWindow(&renderer);
}

void ZeroTextBox::renderDialogue(Kotonoha::RendererRef renderer) {
  session_.DrawMessageText(&renderer, visibleDialogue);
}

void ZeroTextBox::renderIndicator(Kotonoha::RendererRef renderer) {
  session_.DrawMessageCursor(&renderer);
}

// ---------------------------------------------------------------- choices
ZeroChoice::ZeroChoice(Kotonoha::Engine &engine, ZeroSession &session)
    : ChoiceQuestionScene(engine, "", {}, nullptr), session_(session) {}

void ZeroChoice::update() {
  const bool want = session_.Choosing() && Wanted();
  if (!want) {
    if (active)
      hide();
    shown_.clear();
    return;
  }
  const auto labels = Labels();
  if (labels.empty() || (active && labels == shown_))
    return;
  std::vector<Kotonoha::ChoiceOption> choices;
  for (std::size_t i = 0; i < labels.size(); ++i)
    choices.push_back({labels[i], int(i)});
  shown_ = labels;
  setQuestion("", choices,
              [this](int id, const std::string &) { session_.Advance(id); });
  focus_ = InitialFocus();
  selected = std::size_t(std::max(focus_, 0));
  for (const auto &label : labels)
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[choice] %s", label.c_str());
}

void ZeroChoice::Focus(int index) {
  if (index == focus_ || index < 0 || index >= int(options.size()))
    return;
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
bool ZeroChoice::onEvent(Kotonoha::EventRef event) {
  if (!touchActivation.accept(event))
    return true;
  if (!active || options.empty())
    return false;
  layout();
  const int n = int(options.size());
  auto hit = [&](float x, float y) {
    const SDL_FPoint pt{x, y};
    for (int i = 0; i < n; ++i)
      if (SDL_PointInRectFloat(&pt, &options[std::size_t(i)].hitBox))
        return i;
    return -1;
  };
  if (event.type == SDL_EVENT_KEY_DOWN) {
    switch (event.key.key) {
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
      openMenu();
      return true;
    case SDLK_B:
    case SDLK_PAGEUP:
      openBacklog();
      return true;
    case SDLK_UP:
      Focus(focus_ < 0 ? n - 1 : (focus_ + n - 1) % n);
      return true;
    case SDLK_DOWN:
      Focus(focus_ < 0 ? 0 : (focus_ + 1) % n);
      return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
    case SDLK_Z:
      if (focus_ >= 0) {
        select(std::size_t(focus_));
        return true;
      }
      break;
    default:
      break;
    }
  } else if (WheelUp(event)) {
    openBacklog();
    return true;
  } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
    Focus(hit(event.motion.x, event.motion.y));
    return true;
  } else if (RightClick(event)) {
    openMenu();
    return true;
  } else if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
              event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
             event.button.button == SDL_BUTTON_LEFT) {
    if (const int i = hit(event.button.x, event.button.y);
        i >= 0 &&
        Kotonoha::UI::clicked(event, options[std::size_t(i)].hitBox)) {
      select(std::size_t(i));
      return true;
    }
  }
  return false;
}

// SelectWindow (ENVINIT selectopt): main_select-off / -on, 400x50, centred;
// button i at top = area.h / (n + 1) * (i + 1) - 25 inside the selopt area.
bool ZeroSelectChoice::Wanted() const {
  return !session_.Story()->Moving() && !session_.Story()->Roulette() &&
         !session_.Story()->Battling();
}

std::vector<std::string> ZeroSelectChoice::Labels() const {
  std::vector<std::string> labels = session_.Story()->Choices();
  for (auto &label : labels)
    label = session_.Translate(label);
  return labels;
}

void ZeroSelectChoice::layout() {
  constexpr int kSelectW = 400, kSelectH = 50;
  const SDL_Rect area = session_.Story()->SelectArea();
  const int n = int(options.size());
  const int h = area.h / (n + 1);
  for (int i = 0; i < n; ++i)
    options[std::size_t(i)].hitBox = {
        float(kGameWidth / 2 - kSelectW / 2),
        float(area.y + h * (i + 1) - kSelectH / 2), float(kSelectW),
        float(kSelectH)};
}

void ZeroSelectChoice::renderOption(Kotonoha::RendererRef renderer,
                                    std::size_t index, bool focused) {
  const auto &option = options[index];
  const SDL_FRect &b = option.hitBox;
  const bool on = focused && focus_ >= 0;
  session_.DrawNormal(&renderer, on ? "MAIN_SELECT-ON" : "MAIN_SELECT-OFF", b.x,
                      b.y);
  const SDL_Color black{0, 0, 0, 255};
  const SDL_FPoint size =
      session_.DrawText(&renderer, option.label, 0, 0, 24, black, true);
  session_.DrawText(&renderer, option.label, b.x + (b.w - size.x) / 2,
                    b.y + (b.h - size.y) / 2, 24, black);
}

// MoveState (map): MOVE_MOVE_OFF / _ON at (39, 70 + 60n), 247x48, with the
// destination name picture; the focused one shows its pose at SCWIDTH / 2 +
// 132.
bool ZeroMoveChoice::Wanted() const { return session_.Story()->Moving(); }

std::vector<std::string> ZeroMoveChoice::Labels() const {
  std::vector<std::string> labels;
  for (const auto &dest : session_.Story()->MoveDestinations())
    labels.push_back(dest.text);
  return labels;
}

void ZeroMoveChoice::layout() {
  for (std::size_t i = 0; i < options.size(); ++i)
    options[i].hitBox = {39, 70 + 60 * float(i), 247, 48};
}

void ZeroMoveChoice::renderPanel(Kotonoha::RendererRef renderer) {
  const Game &game = *session_.Story();
  const auto &catalog = session_.Disc().Source().Catalog();
  const int back = catalog.ResolveStage(game.MoveBack());
  if (Picture *p = back >= 0 ? session_.SceneIndexPicture(back)
                             : session_.NamedScene(game.MoveBack())) {
    const SDL_FRect dst{0, 0, p->Width(), p->Height()};
    p->Draw(&renderer, nullptr, &dst);
  }
  const auto &dest = game.MoveDestinations();
  if (focus_ >= 0 && focus_ < int(dest.size()) &&
      !dest[std::size_t(focus_)].pose.empty())
    if (Picture *pose = session_.SceneIndexPicture(
            catalog.ResolveSprite(dest[std::size_t(focus_)].pose))) {
      const SDL_FRect dst{float(kGameWidth / 2 + 132 - int(pose->Width()) / 2),
                          0, pose->Width(), pose->Height()};
      pose->Draw(&renderer, nullptr, &dst);
    }
}

void ZeroMoveChoice::renderOption(Kotonoha::RendererRef renderer,
                                  std::size_t index, bool focused) {
  const auto &option = options[index];
  const bool on = focused && focus_ >= 0;
  session_.DrawNormal(&renderer, on ? "MOVE_MOVE_ON" : "MOVE_MOVE_OFF",
                      option.hitBox.x, option.hitBox.y);
  char name[48];
  SDL_snprintf(name, sizeof name, "MOVETEXT_POINT%02d_%s",
               ZeroSession::MovePosition(option.label), on ? "ON" : "OFF");
  DrawCentered(&renderer, session_.NormalPicture(name), option.hitBox, -2);
}

// ---------------------------------------------------------------- battle
bool ZeroBattleChoice::Wanted() const { return session_.Story()->Battling(); }

std::vector<std::string> ZeroBattleChoice::Labels() const {
  return session_.Story()->Choices();
}

void ZeroBattleChoice::layout() {
  for (std::size_t i = 0; i < options.size(); ++i) {
    const int col = int(i % 2);
    const int row = int(i / 2);
    options[i].hitBox = {340.0f + float(col) * 140.0f,
                         330.0f + float(row) * 48.0f, 130.0f, 40.0f};
  }
}

void ZeroBattleChoice::renderPanel(Kotonoha::RendererRef renderer) {
  const auto &b = session_.Story()->Battle();
  if (!b.back.empty()) {
    if (Picture *p = session_.NamedScene(b.back)) {
      const SDL_FRect dst{0, 0, float(kGameWidth), float(kGameHeight)};
      p->Draw(&renderer, nullptr, &dst);
    }
  }
  SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(&renderer, 12, 16, 28, 165);
  const SDL_FRect shade{0, 0, float(kGameWidth), float(kGameHeight)};
  SDL_RenderFillRect(&renderer, &shade);

  // Enemy sprites
  for (std::size_t i = 0; i < b.enemies.size(); ++i) {
    const auto &e = b.enemies[i];
    if (e.hp <= 0 || e.pose.empty())
      continue;
    if (Picture *p = session_.NamedScene(e.pose)) {
      const float cx =
          float(kGameWidth) * (float(i) + 1.0f) / float(b.enemies.size() + 1);
      const SDL_FRect dst{cx - p->Width() * 0.5f, 40.0f, p->Width(),
                          p->Height()};
      p->Draw(&renderer, nullptr, &dst);
    }
  }

  auto drawBar = [&](float x, float y, float w, const std::string &label,
                     int hp, int maxHp, SDL_Color fill) {
    const SDL_FRect bg{x, y, w, 18.0f};
    SDL_SetRenderDrawColor(&renderer, 25, 25, 35, 220);
    SDL_RenderFillRect(&renderer, &bg);
    const float ratio =
        maxHp > 0 ? std::clamp(float(hp) / float(maxHp), 0.0f, 1.0f) : 0.0f;
    const SDL_FRect fg{x + 2.0f, y + 2.0f, (w - 4.0f) * ratio, 14.0f};
    SDL_SetRenderDrawColor(&renderer, fill.r, fill.g, fill.b, 255);
    SDL_RenderFillRect(&renderer, &fg);
    SDL_SetRenderDrawColor(&renderer, 220, 220, 235, 255);
    SDL_RenderRect(&renderer, &bg);
    const std::string txt =
        label + "  " + std::to_string(hp) + "/" + std::to_string(maxHp);
    session_.DrawText(&renderer, txt, x + 6.0f, y - 18.0f, 16,
                      {255, 255, 255, 255});
  };

  // Enemy HP bars at top
  for (std::size_t i = 0; i < b.enemies.size(); ++i) {
    const auto &e = b.enemies[i];
    drawBar(24.0f + float(i) * 200.0f, 28.0f, 184.0f, e.name, e.hp, e.maxHp,
            {210, 60, 70, 255});
  }

  // Bottom HUD panel
  const SDL_FRect hud{16.0f, 294.0f, float(kGameWidth - 32), 140.0f};
  SDL_SetRenderDrawColor(&renderer, 18, 24, 42, 225);
  SDL_RenderFillRect(&renderer, &hud);
  SDL_SetRenderDrawColor(&renderer, 140, 175, 230, 255);
  SDL_RenderRect(&renderer, &hud);

  const std::string header = "Turn " + std::to_string(b.turn) + " - " + b.log;
  session_.DrawText(&renderer, header, 28.0f, 302.0f, 16, {255, 235, 160, 255});

  // Party HP bars on bottom-left
  for (std::size_t i = 0; i < b.party.size() && i < 3; ++i) {
    const auto &p = b.party[i];
    drawBar(28.0f, 344.0f + float(i) * 34.0f, 280.0f, p.name, p.hp, p.maxHp,
            {70, 195, 110, 255});
  }
}

void ZeroBattleChoice::renderOption(Kotonoha::RendererRef renderer,
                                    std::size_t index, bool focused) {
  const auto &option = options[index];
  const SDL_FRect &b = option.hitBox;
  const bool on = focused && focus_ >= 0;
  SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(&renderer, on ? 70 : 32, on ? 115 : 44, on ? 195 : 74,
                         240);
  SDL_RenderFillRect(&renderer, &b);
  SDL_SetRenderDrawColor(&renderer, on ? 255 : 140, on ? 235 : 165,
                         on ? 140 : 210, 255);
  SDL_RenderRect(&renderer, &b);
  const SDL_Color white{255, 255, 255, 255};
  const SDL_FPoint size =
      session_.DrawText(&renderer, option.label, 0, 0, 18, white, true);
  session_.DrawText(&renderer, option.label, b.x + (b.w - size.x) * 0.5f,
                    b.y + (b.h - size.y) * 0.5f, 18, white);
}

// ---------------------------------------------------------------- roulette
bool ZeroRoulette::onEvent(Kotonoha::EventRef event) {
  if (!session_.RouletteActive())
    return false;
  session_.RouletteEvent(event);
  return true;
}

// ---------------------------------------------------------------- in-game
// screens
ZeroCommandMenu::ZeroCommandMenu(Kotonoha::Engine &engine, ZeroSession &session)
    : InGameMenu(engine), session_(session) {}

bool ZeroCommandMenu::onEvent(Kotonoha::EventRef event) {
  if (IsOpen()) {
    session_.ScreenEvent(NativeNavigation(engine, event));
    return true;
  } else if (session_.Playing()) {
    return InGameMenu::onEvent(event);
  }
  return false;
}

// "Title" in CommandState and the end of the story leave through InGameMenu.
void ZeroCommandMenu::update() {
  if (session_.TakeTitleRequest() || session_.Ended())
    activate(5);
}

ZeroBacklog::ZeroBacklog(Kotonoha::Engine &engine, ZeroSession &session)
    : VNBacklog(engine), session_(session) {}

bool ZeroBacklog::onEvent(Kotonoha::EventRef event) {
  if (isVisible()) {
    session_.ScreenEvent(NativeNavigation(engine, event));
    return true;
  } else if (session_.Playing() && !session_.ScreenActive()) {
    return VNBacklog::onEvent(event);
  }
  return false;
}

} // namespace ZeroNoTsukaima
