#include "SnowComponents.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>

#include <algorithm>

namespace SnowRadish {
namespace {

bool IsKey(Kotonoha::EventRef e, SDL_Keycode key) { return e.type == SDL_EVENT_KEY_DOWN && e.key.key == key; }

bool WheelUp(Kotonoha::EventRef e) {
  if (e.type != SDL_EVENT_MOUSE_WHEEL) return false;
  const float y = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
  return y > 0;
}

bool RightClick(Kotonoha::EventRef e) {
  return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT;
}

void Fill(SDL_Renderer *r, const SDL_FRect &rect, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(r, &rect);
}

constexpr SDL_Color kWhite{255, 255, 255, 255}, kGold{255, 230, 140, 255};

} // namespace

// ---------------------------------------------------------------- title

SnowTitleMenu::SnowTitleMenu(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links)
    : GameMenu(engine), session_(session), links_(links) {}

bool SnowTitleMenu::isVisible() const { return !session_.Playing(); }

void SnowTitleMenu::onEvent(Kotonoha::EventRef event) {
  if (isVisible()) session_.FrontendEvent(event);
}

void SnowTitleMenu::update() {
  if (!isVisible()) return;
  if (const auto action = session_.FrontendTick()) switch (*action) {
    case SnowSession::TitleAction::NewGame: activate(0); break;
    case SnowSession::TitleAction::Load: activate(1); break;
    case SnowSession::TitleAction::Quit: activate(3); break;
    }
}

void SnowTitleMenu::activate(int index) {
  if (index == 0) {
    if (session_.Start()) GameMenu::activate(0);
  } else if (index == 1) {
    links_.openLoad();
  } else if (index == 3) {
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
  } else {
    GameMenu::activate(index);
  }
}

void SnowTitleMenu::renderTitle(Kotonoha::RendererRef renderer) { session_.DrawFrontend(&renderer); }

// ---------------------------------------------------------------- stage

void SnowStage::render(Kotonoha::RendererRef renderer, float) { session_.DrawStage(&renderer); }

// ---------------------------------------------------------------- text box

SnowTextBox::SnowTextBox(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links)
    : VNTextBox(engine, "", ""), session_(session), links_(links) {
  hide();
}

void SnowTextBox::recordDialogue(const std::string &speaker, const std::string &text) {
  if (!text.empty() && links_.record) links_.record(speaker, text);
}

void SnowTextBox::SyncPage() {
  if (!session_.HasMessage() || session_.Choosing()) {
    hide();
    pageKey_.clear();
    return;
  }
  show();
  const std::string speaker = session_.Speaker(), text = session_.Text();
  const std::string key = speaker + "\n" + text;
  if (key == pageKey_) return;
  pageKey_ = key;
  setDialogue(speaker, text);
  completeDialogue();
  doneAt_ = SDL_GetTicks();
}

// Auto mode waits for the voice and then the auto delay; Ctrl skips pages and audio waits.
void SnowTextBox::update() {
  SyncPage();
  VNTextBox::update();
  const Uint64 now = SDL_GetTicks();
  if (!session_.Playing()) return;
  if (session_.Choosing() || engine.isStoryPaused()) {
    doneAt_ = now;
    return;
  }
  if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0 && session_.CanSkip()) {
    if (session_.WaitingForClick()) session_.Advance(-1);
    else session_.Skip();
    return;
  }
  if (session_.VoiceActive() || session_.Fading()) doneAt_ = now;
  if (session_.Auto() && session_.WaitingForClick() && now - doneAt_ >= Uint64(session_.AutoDelayMs())) {
    doneAt_ = now;
    session_.Advance(-1);
  }
}

bool SnowTextBox::handleEvent(Kotonoha::EventRef event) {
  if (!session_.Playing() || session_.Choosing()) return false;
  if (IsKey(event, SDLK_PAGEUP) || WheelUp(event)) {
    openBacklog();
    return true;
  }
  if (RightClick(event)) {
    openMenu();
    return true;
  }
  if (IsKey(event, SDLK_A) && !event.key.repeat) {
    session_.SetAuto(!session_.Auto());
    doneAt_ = SDL_GetTicks();
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
    switch (session_.MessageWindowClick(event.button.x, event.button.y)) {
    case SnowSession::WindowClick::ToggleAuto:
      session_.SetAuto(!session_.Auto());
      doneAt_ = SDL_GetTicks();
      return true;
    case SnowSession::WindowClick::Consumed: return true;
    case SnowSession::WindowClick::None: break;
    }
  }
  const bool advance = IsKey(event, SDLK_RETURN) || IsKey(event, SDLK_KP_ENTER) || IsKey(event, SDLK_SPACE) ||
                       IsKey(event, SDLK_DOWN) ||
                       (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT);
  if (!advance) return false;
  if (session_.Auto()) session_.SetAuto(false);
  else session_.Advance(-1);
  return true;
}

void SnowTextBox::renderFrame(Kotonoha::RendererRef renderer) { session_.DrawMessageWindow(&renderer); }

void SnowTextBox::renderDialogue(Kotonoha::RendererRef renderer) {
  session_.DrawMessageText(&renderer, speakerName, visibleDialogue);
}

void SnowTextBox::renderIndicator(Kotonoha::RendererRef renderer) { session_.DrawMessageCursor(&renderer); }

// ---------------------------------------------------------------- choices

SnowChoice::SnowChoice(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links,
                       SnowSession::ChoiceKind kind)
    : ChoiceQuestionScene(engine, "", {}, nullptr), session_(session), links_(links), kind_(kind) {}

void SnowChoice::update() {
  const bool want = session_.Playing() && session_.Choosing() && session_.Kind() == kind_;
  if (!want) {
    if (active) hide();
    shown_.clear();
    return;
  }
  const auto labels = session_.Choices();
  if (labels.empty() || (active && labels == shown_)) return;
  std::vector<Kotonoha::ChoiceOption> choices;
  for (std::size_t i = 0; i < labels.size(); ++i) choices.push_back({labels[i], int(i)});
  shown_ = labels;
  setQuestion("", choices, [this](int id, const std::string &) { session_.Advance(id); });
  session_.SetFocus(session_.InitialFocus());
  selected = std::size_t(std::max(Focused(), 0));
  for (const auto &label : labels) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[choice] %s", label.c_str());
}

int SnowChoice::Hit(float x, float y) {
  const SDL_FPoint pt{x, y};
  for (std::size_t i = 0; i < options.size(); ++i)
    if (SDL_PointInRectFloat(&pt, &options[i].hitBox)) return int(i);
  return -1;
}

void SnowChoice::Focus(int index) {
  if (index >= int(options.size())) return;
  session_.SetFocus(index);
  if (index >= 0) selected = std::size_t(index);
}

void SnowChoice::select(std::size_t index) {
  if (index >= options.size()) return;
  ChoiceQuestionScene::select(index);
}

// Up/Left and Down/Right cycle, Enter / Space pick the focused option.
bool SnowChoice::Key(SDL_Keycode key) {
  const int n = int(options.size()), focus = Focused();
  switch (key) {
  case SDLK_UP:
  case SDLK_LEFT: Focus(focus <= 0 ? n - 1 : focus - 1); return true;
  case SDLK_DOWN:
  case SDLK_RIGHT: Focus(focus < 0 || focus >= n - 1 ? 0 : focus + 1); return true;
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
  case SDLK_SPACE:
    if (focus >= 0) select(std::size_t(focus));
    return true;
  default: return false;
  }
}

void SnowChoice::onEvent(Kotonoha::EventRef event) {
  if (!active || options.empty()) return;
  layout();
  if (event.type == SDL_EVENT_KEY_DOWN) {
    if (event.key.key == SDLK_ESCAPE) return openMenu();
    if (event.key.key == SDLK_B || event.key.key == SDLK_PAGEUP) return openBacklog();
    if (!Blocked()) Key(event.key.key);
  } else if (WheelUp(event)) {
    openBacklog();
  } else if (RightClick(event)) {
    openMenu();
  } else if (Blocked()) {
    return;
  } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
    const int hit = Hit(event.motion.x, event.motion.y);
    if (hit >= 0 || kind_ != SnowSession::ChoiceKind::Text) Focus(hit);
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
    const int hit = Hit(event.button.x, event.button.y);
    Focus(hit);
    if (hit >= 0) select(std::size_t(hit));
  }
}

void SnowTextChoice::layout() {
  for (std::size_t i = 0; i < options.size(); ++i) options[i].hitBox = session_.WindowItem(i);
}

bool SnowTextChoice::Key(SDL_Keycode key) {
  if (key >= SDLK_A && key <= SDLK_I && int(key - SDLK_A) < int(options.size())) {
    Focus(int(key - SDLK_A));
    return true;
  }
  if (key == SDLK_LEFT || key == SDLK_RIGHT) return false;
  return SnowChoice::Key(key);
}

void SnowTextChoice::renderOption(Kotonoha::RendererRef renderer, std::size_t index, bool) {
  session_.DrawWindowItem(&renderer, index, int(index) == Focused());
}

void SnowMapChoice::layout() {
  for (std::size_t i = 0; i < options.size(); ++i) options[i].hitBox = session_.Hotspot(i);
}

// The map stays on screen as the "from" picture of the next wipe.
void SnowMapChoice::select(std::size_t index) {
  if (index >= options.size()) return;
  session_.PickMap();
  SnowChoice::select(index);
}

// ---------------------------------------------------------------- pause menu

SnowPauseMenu::SnowPauseMenu(Kotonoha::Engine &engine, SnowSession &session, SnowLinks &links)
    : InGameMenu(engine), session_(session), links_(links) {}

void SnowPauseMenu::onEvent(Kotonoha::EventRef event) {
  if (!session_.Playing()) return;
  if (IsOpen() && RightClick(event)) return close();
  if (IsOpen() && event.type == SDL_EVENT_MOUSE_MOTION) {
    const SDL_FPoint pt{event.motion.x, event.motion.y};
    for (int i = 0; i < itemCount(); ++i) {
      const SDL_FRect rect = itemRect(i);
      if (SDL_PointInRectFloat(&pt, &rect)) selected = i;
    }
  }
  InGameMenu::onEvent(event);
}

// The end of the story returns to the title through the menu item.
void SnowPauseMenu::update() {
  if (session_.Playing() && session_.Ended()) activate(5);
}

std::string SnowPauseMenu::itemLabel(int index) const {
  static const char *labels[]{"Continue", "History [B]", "Settings", "Save...", "Load...",
                              "Back to title"};
  return index >= 0 && index < 6 ? labels[index] : "";
}

SDL_FRect SnowPauseMenu::itemRect(int index) const { return {250, 150.0f + float(index) * 56, 300, 44}; }

void SnowPauseMenu::renderPanel(Kotonoha::RendererRef renderer) {
  Fill(&renderer, {0, 0, kGameWidth, kGameHeight}, {0, 0, 40, 150});
  Fill(&renderer, {230, 80, 340, 420}, {10, 20, 60, 220});
  session_.DrawText(&renderer, "Paused", 250, 96, 28, kGold);
}

void SnowPauseMenu::renderItem(Kotonoha::RendererRef renderer, int index, bool focused) {
  const SDL_FRect rect = itemRect(index);
  Fill(&renderer, rect, focused ? SDL_Color{0, 149, 149, 230} : SDL_Color{0, 0, 0, 160});
  const std::string label = itemLabel(index);
  const SDL_FPoint size = session_.DrawText(&renderer, label, 0, 0, 22, {}, true);
  session_.DrawText(&renderer, label, rect.x + (rect.w - size.x) / 2, rect.y + (rect.h - size.y) / 2, 22, kWhite);
}

// ---------------------------------------------------------------- backlog

void SnowBacklog::onEvent(Kotonoha::EventRef event) {
  if (!session_.Playing()) return;
  if (isVisible() && (RightClick(event) || IsKey(event, SDLK_ESCAPE))) return close();
  if (!isVisible() && (IsKey(event, SDLK_PAGEUP) || WheelUp(event))) return open();
  if (isVisible() && IsKey(event, SDLK_PAGEDOWN)) {
    scroll = std::min(maxScroll, scroll + 400);
    return;
  }
  VNBacklog::onEvent(event);
}

void SnowBacklog::renderFrame(Kotonoha::RendererRef renderer) {
  Fill(&renderer, {0, 0, kGameWidth, kGameHeight}, {0, 0, 30, 220});
  session_.DrawText(&renderer, "History", 40, 24, 28, kGold);
  session_.DrawText(&renderer, "[B / Esc / right click] close", 440, 32, 18, {200, 200, 200, 255});
}

std::vector<std::string> SnowBacklog::Wrap(SDL_Renderer *r, const std::string &text, float width) {
  std::vector<std::string> lines;
  std::string line;
  for (std::size_t i = 0; i < text.size();) {
    std::size_t next = i + 1;
    while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
    const std::string ch = text.substr(i, next - i);
    i = next;
    if (ch == "\n") {
      lines.push_back(std::move(line));
      line.clear();
      continue;
    }
    if (!line.empty() && session_.DrawText(r, line + ch, 0, 0, 20, {}, true).x > width) {
      lines.push_back(std::move(line));
      line.clear();
    }
    line += ch;
  }
  if (!line.empty()) lines.push_back(std::move(line));
  return lines;
}

void SnowBacklog::renderEntries(Kotonoha::RendererRef renderer) {
  constexpr float kTop = 80, kHeight = 500, kLeft = 40, kWidth = 720, kLine = 26;
  std::vector<std::pair<std::string, SDL_Color>> rows;
  for (const auto &entry : entries) {
    if (!entry.speaker.empty()) rows.emplace_back(entry.speaker, kGold);
    for (auto &line : Wrap(&renderer, entry.text, kWidth)) rows.emplace_back(std::move(line), kWhite);
    rows.emplace_back(std::string(), kWhite);
  }
  maxScroll = std::max(0.0f, float(rows.size()) * kLine - kHeight);
  scroll = scrollToBottom ? maxScroll : std::clamp(scroll, 0.0f, maxScroll);
  scrollToBottom = false;
  const SDL_Rect clip{int(kLeft), int(kTop), int(kWidth), int(kHeight)};
  SDL_SetRenderClipRect(&renderer, &clip);
  float y = kTop - scroll;
  for (const auto &[text, color] : rows) {
    if (y + kLine >= kTop && y < kTop + kHeight && !text.empty())
      session_.DrawText(&renderer, text, kLeft, y, 20, color);
    y += kLine;
  }
  SDL_SetRenderClipRect(&renderer, nullptr);
}

} // namespace SnowRadish
