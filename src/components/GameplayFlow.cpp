#include <Kotonoha/components/GameplayFlow.hpp>
#include <Kotonoha/components/Panel.hpp>
#include <Kotonoha/Kotonoha.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/debug/McpServer.hpp>

namespace Kotonoha {

bool GameSession::RunDebugUi(::Kotonoha::Kotonoha *app, Kotonoha_Game &game) {
  return Debug::Run(app, RunningGameplay(), game);
}

// Logical presentation is per render target; the viewport belongs to the window.
// The first `n` UTF-8 code points of `text`.
static std::string Utf8Prefix(const std::string &text, size_t n) {
  size_t i = 0;
  for (size_t cp = 0; i < text.size() && cp < n; ++cp) {
    ++i;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
  }
  return text.substr(0, i);
}

static size_t Utf8Length(const std::string &text) {
  size_t n = 0;
  for (unsigned char c : text) n += (c & 0xC0) != 0x80;
  return n;
}

static void SetWindowPresentation(SDL_Renderer *r, int w, int h, SDL_RendererLogicalPresentation mode) {
  SDL_Texture *target = SDL_GetRenderTarget(r);
  if (target != nullptr) SDL_SetRenderTarget(r, nullptr);
  SDL_SetRenderLogicalPresentation(r, w, h, mode);
  if (target != nullptr) SDL_SetRenderTarget(r, target);
}

GameplayFlow::GameViewport::GameViewport(SDL_Renderer *r, const Config &config) : renderer(r) {
  SetWindowPresentation(renderer, static_cast<int>(config.width), static_cast<int>(config.height),
                        SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

GameplayFlow::GameViewport::~GameViewport() {
  SetWindowPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
}

GameplayFlow::GameplayFlow(Config config) : config_(std::move(config)), menu_(config_.id, config_.saveSlots) {
  menu_.SetGameTitle(config_.title);
  textBox_.SetStyle(config_.textBox);
}

GameplayFlow::~GameplayFlow() { Debug::ForgetSceneCanvas(&canvas_); }

bool GameplayFlow::Attach(std::unique_ptr<GameSession> session) {
  session_ = std::move(session);
  playing_ = false;
  // A front end shows first: start the story silently (validation only).
  return session_ != nullptr && Begin(!session_->HasFrontend());
}

bool GameplayFlow::Begin(bool sync) {
  if (!session_ || !session_->Start()) return false;
  titleItems_.clear();
  frontend_ = false;
  inputs_.clear();
  lastLogged_.clear();
  pageKey_.clear();
  pageComplete_ = true;
  if (auto_) {
    auto_ = false;
    session_->AutoChanged(false);
  }
  choices_.Close();
  choicesOpen_ = false;
  playing_ = true;
  if (sync) session_->Sync();
  return true;
}

void GameplayFlow::Advance(int choice) {
  const int in = session_->Choosing() ? choice : -1;
  if (session_->Choosing() && in < 0) return;
  if (in < 0 && !pageComplete_) {
    // The first click completes a page that is still being revealed.
    pageComplete_ = true;
    pageDoneAt_ = SDL_GetTicks();
    return;
  }
  if (in < 0 && session_->Skip()) return;
  inputs_.push_back(in);
  session_->Step(in);
}

void GameplayFlow::Replay(const std::vector<int> &inputs) {
  session_->SetReplaying(true);
  Begin();
  for (int in : inputs) {
    session_->Step(in);
    inputs_.push_back(in);
  }
  session_->SetReplaying(false);
  session_->Sync();
}

void GameplayFlow::ShowTitle() {
  playing_ = false;
  choices_.Close();
  choicesOpen_ = false;
  titleHover_ = -1;
  if (session_->HasFrontend()) {
    titleItems_.clear();
    frontend_ = true;
    menu_.Hide();
    session_->EnterFrontend(!booted_);
    booted_ = true;
    return;
  }
  titleItems_ = session_->TitleItems(menu_.Store().AnySave());
  if (titleItems_.empty()) menu_.Show(GameMenu::Page::Title, font_);
  else menu_.Hide();
}

void GameplayFlow::PickTitle(size_t index) {
  if (index < titleItems_.size()) RunTitleAction(titleItems_[index].action);
}

void GameplayFlow::RunTitleAction(GameSession::TitleAction action) {
  switch (action) {
  case GameSession::TitleAction::NewGame: Begin(); break;
  case GameSession::TitleAction::Load: menu_.Show(GameMenu::Page::LoadSlots, font_); break;
  case GameSession::TitleAction::Quit: {
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
    break;
  }
  }
}

void GameplayFlow::HandleRequest(const GameSession::FlowRequest &request) {
  switch (request.kind) {
  case GameSession::FlowRequest::Save: {
    std::string label = session_->SaveLabel();
    if (label.empty()) label = SaveStore::MakeLabel(textBox_.Body(), 14);
    menu_.Store().Write(request.slot, label.empty() ? "-" : label, inputs_);
    SDL_Log("[save] slot %d (%zu inputs)", request.slot + 1, inputs_.size());
    break;
  }
  case GameSession::FlowRequest::Load: {
    std::vector<int> in;
    if (menu_.Store().Read(request.slot, nullptr, &in)) {
      Replay(in);
      SDL_Log("[load] slot %d (%zu inputs)", request.slot + 1, in.size());
    }
    break;
  }
  case GameSession::FlowRequest::Title: ShowTitle(); break;
  case GameSession::FlowRequest::None: break;
  }
}

bool GameplayFlow::OpenScreen(GameSession::Screen screen) {
  if (auto_) {
    auto_ = false;
    session_->AutoChanged(false);
  }
  return session_->OpenScreen(screen);
}

// Typewriter reveal, auto mode and Ctrl skip, run once per frame while playing.
void GameplayFlow::Pace() {
  const uint64_t now = SDL_GetTicks();
  const std::string key = session_->HasMessage() ? session_->Speaker() + "\n" + session_->Text() : std::string();
  const int delay = session_->TextDelayMs();
  if (key != pageKey_) {
    // A page that only grew (text appended after a wait) keeps its revealed part.
    const bool grew = !pageKey_.empty() && key.compare(0, pageKey_.size(), pageKey_) == 0;
    const size_t shown = grew ? pageLength_ : 0;
    pageKey_ = key;
    pageLength_ = Utf8Length(session_->Text());
    pageComplete_ = delay <= 0 || pageLength_ <= shown;
    pageStart_ = now - static_cast<uint64_t>(shown) * static_cast<uint64_t>(SDL_max(delay, 0));
    pageDoneAt_ = now;
  }
  if (!pageComplete_ && now - pageStart_ >= static_cast<uint64_t>(delay) * pageLength_) {
    pageComplete_ = true;
    pageDoneAt_ = now;
  }
  const bool idle = !menu_.Active() && !session_->ScreenActive() && !session_->Choosing();
  if (!idle) {
    pageDoneAt_ = now;
    return;
  }
  if ((SDL_GetModState() & SDL_KMOD_CTRL) != 0 && session_->CanSkip()) {
    pageComplete_ = true;
    if (session_->WaitingForClick()) Advance(-1);
    else session_->Skip();
    return;
  }
  if (session_->VoiceActive() || !pageComplete_) pageDoneAt_ = now;
  if (auto_ && session_->WaitingForClick() && now - pageDoneAt_ >= static_cast<uint64_t>(session_->AutoDelayMs())) {
    pageDoneAt_ = now;
    Advance(-1);
  }
}

void GameplayFlow::HandleTitleEvent(SDL_Event *e, Kotonoha_Game *game) {
  const int n = static_cast<int>(titleItems_.size());
  if (e->type == SDL_EVENT_MOUSE_MOTION || e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    SDL_Event c = *e;
    SDL_ConvertEventToRenderCoordinates(game->render, &c);
    const SDL_FPoint pt = e->type == SDL_EVENT_MOUSE_MOTION ? SDL_FPoint{c.motion.x, c.motion.y}
                                                            : SDL_FPoint{c.button.x, c.button.y};
    int hit = -1;
    for (int i = 0; i < n; ++i)
      if (SDL_PointInRectFloat(&pt, &titleItems_[static_cast<size_t>(i)].rect)) hit = i;
    titleHover_ = hit;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT && hit >= 0)
      PickTitle(static_cast<size_t>(hit));
  } else if (e->type == SDL_EVENT_KEY_DOWN) {
    if (e->key.key == SDLK_UP) titleHover_ = titleHover_ <= 0 ? n - 1 : titleHover_ - 1;
    else if (e->key.key == SDLK_DOWN) titleHover_ = titleHover_ < 0 || titleHover_ >= n - 1 ? 0 : titleHover_ + 1;
    else if ((e->key.key == SDLK_RETURN || e->key.key == SDLK_SPACE) && titleHover_ >= 0)
      PickTitle(static_cast<size_t>(titleHover_));
  }
}

void GameplayFlow::OnMenu(const GameMenu::Event &ev) {
  switch (ev.action) {
  case GameMenu::Action::NewGame: Begin(); break;
  case GameMenu::Action::Title: ShowTitle(); break;
  case GameMenu::Action::Quit: {
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
    break;
  }
  case GameMenu::Action::Save: {
    const std::string label = SaveStore::MakeLabel(textBox_.Body(), 14);
    menu_.Store().Write(ev.slot, label.empty() ? "-" : label, inputs_);
    SDL_Log("[save] slot %d (%zu inputs)", ev.slot + 1, inputs_.size());
    break;
  }
  case GameMenu::Action::Load: {
    std::vector<int> in;
    if (menu_.Store().Read(ev.slot, nullptr, &in)) {
      Replay(in);
      SDL_Log("[load] slot %d (%zu inputs)", ev.slot + 1, in.size());
    }
    break;
  }
  default: break;
  }
}

SDL_AppResult GameplayFlow::HandleEvent(SDL_Event *e, Kotonoha_Game *game) {
  if (!session_) return SDL_APP_CONTINUE;
  if (session_->OwnsFrontend()) return session_->HandleEvent(e, game);
  // Pointer coordinates are resolved against the game viewport.
  GameViewport viewport(game->render, config_);
  if (menu_.Active()) {
    menu_.HandleEvent(e, game);
    return SDL_APP_CONTINUE;
  }
  if (!playing_) {
    if (frontend_) {
      SDL_Event c = *e;
      SDL_ConvertEventToRenderCoordinates(game->render, &c);
      session_->FrontendEvent(c);
    } else if (!titleItems_.empty()) {
      HandleTitleEvent(e, game);
    }
    return SDL_APP_CONTINUE;
  }
  if (session_->ScreenActive()) {
    SDL_Event c = *e;
    SDL_ConvertEventToRenderCoordinates(game->render, &c);
    session_->ScreenEvent(c);
    return SDL_APP_CONTINUE;
  }
  const bool command = (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE) ||
                       (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_RIGHT);
  const bool history = (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_PAGEUP) ||
                       (e->type == SDL_EVENT_MOUSE_WHEEL && e->wheel.y > 0);
  if (history && OpenScreen(GameSession::Screen::History)) return SDL_APP_CONTINUE;
  if (session_->Choosing()) {
    if (session_->OwnsChoices()) {
      if (command) {
        if (!OpenScreen(GameSession::Screen::Command)) menu_.Show(GameMenu::Page::Pause, font_);
        return SDL_APP_CONTINUE;
      }
      SDL_Event c = *e;
      SDL_ConvertEventToRenderCoordinates(game->render, &c);
      const int pick = session_->ChoiceEvent(c);
      if (pick >= 0) Advance(pick);
      return SDL_APP_CONTINUE;
    }
    choices_.HandleEvent(e, game);
    return SDL_APP_CONTINUE;
  }
  if (command) {
    if (!OpenScreen(GameSession::Screen::Command)) menu_.Show(GameMenu::Page::Pause, font_);
    return SDL_APP_CONTINUE;
  }
  auto windowClick = GameSession::WindowClick::None;
  if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_LEFT && session_->HasMessage()) {
    SDL_Event c = *e;
    SDL_ConvertEventToRenderCoordinates(game->render, &c);
    windowClick = session_->MessageWindowClick(c.button.x, c.button.y);
    if (windowClick == GameSession::WindowClick::Consumed) return SDL_APP_CONTINUE;
  }
  const bool advance = (e->type == SDL_EVENT_KEY_DOWN && (e->key.key == SDLK_RETURN || e->key.key == SDLK_SPACE ||
                                                          e->key.key == SDLK_DOWN)) ||
                       e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  if ((windowClick == GameSession::WindowClick::ToggleAuto ||
       (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_A && !e->key.repeat)) &&
      session_->AutoDelayMs() >= 0) {
    auto_ = !auto_;
    pageDoneAt_ = SDL_GetTicks();
    session_->AutoChanged(auto_);
  } else if (advance && auto_) {
    // Any click stops auto mode (KAG cancelAuto).
    auto_ = false;
    session_->AutoChanged(false);
  } else if (advance) {
    Advance(-1);
  }
  return SDL_APP_CONTINUE;
}

void GameplayFlow::SyncChoices() {
  const bool want = session_->Choosing() && !menu_.Active() && !session_->OwnsChoices();
  if (want && !choicesOpen_) {
    const auto items = session_->Choices();
    choices_.Open(items, font_);
    choicesOpen_ = true;
    for (const auto &c : items) SDL_Log("[choice] %s", c.c_str());
  } else if (!session_->Choosing() && choicesOpen_) {
    choices_.Close();
    choicesOpen_ = false;
  }
}

void GameplayFlow::Frame(Kotonoha_Game *game, TTF_Font *font) {
  if (!session_ || session_->OwnsFrontend()) return;
  if (!font_ && font) {
    font_ = font;
    textBox_.SetFont(font_);
    ShowTitle();
  }

  if (playing_ && session_->Ended()) ShowTitle();
  if (playing_) {
    session_->Idle();
    session_->Sync();
    SyncChoices();
    Pace();
    textBox_.SetCaption(session_->Caption());
    if (session_->HasMessage()) {
      const std::string &text = session_->Text();
      const int delay = session_->TextDelayMs();
      if (pageComplete_ || delay <= 0) {
        textBox_.SetMessage(session_->Speaker(), text);
      } else {
        const size_t shown = static_cast<size_t>((SDL_GetTicks() - pageStart_) / static_cast<uint64_t>(delay));
        textBox_.SetMessage(session_->Speaker(), Utf8Prefix(text, shown));
      }
      const std::string key = session_->Speaker() + session_->Text();
      if (key != lastLogged_) {
        lastLogged_ = key;
        SDL_Log("[text] %s%s%s", session_->Speaker().c_str(), session_->Speaker().empty() ? "" : ": ",
                session_->Text().c_str());
      }
    } else {
      textBox_.SetMessage("", "");
    }
    if (choicesOpen_ && choices_.Result() >= 0) {
      const int pick = choices_.Result();
      choices_.Close();
      choicesOpen_ = false;
      Advance(pick);
    }
  }
  if (!playing_ && frontend_ && !menu_.Active())
    if (const auto action = session_->FrontendTick()) RunTitleAction(*action);
  session_->Tick();
  HandleRequest(session_->TakeRequest());
  if (menu_.Active()) OnMenu(menu_.Poll());
  // "Back" from the load page returns to the session's own title, not the generic one.
  if (!playing_ && (frontend_ || !titleItems_.empty()) && menu_.CurrentPage() == GameMenu::Page::Title) menu_.Hide();

  SDL_Renderer *r = game->render;
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  SDL_RenderClear(r);
  const SDL_FRect place = SceneRect(r);
  if (!canvasReady_) {
    canvas_.RegisterCanva(DrawSceneLayer, 0, place, this, "game");
    canvasReady_ = true;
  } else {
    canvas_.UpdateCanva(DrawSceneLayer, -1, place);
  }
  canvas_.RenderCanvas(game->window, r);
  Debug::SetSceneCanvas(&canvas_);

  GameViewport viewport(r, config_);
  const SDL_FRect all{0, 0, config_.width, config_.height};
  if (playing_) {
    if (textBox_.Visible()) session_->DrawMessageWindow(r);
    textBox_.Draw(r);
    if (session_->Choosing() && session_->OwnsChoices()) session_->DrawChoices(r);
    if (choicesOpen_) {
      DrawDim(r, all, 130);
      const float h = 72.0f * float(session_->Choices().size()) + 36.0f;
      DrawPanel(r, {config_.width * 0.5f - 260.0f, config_.height * 0.5f - h * 0.5f, 520.0f, h});
      choices_.Draw(game->window, r);
    }
    if (session_->ScreenActive()) session_->DrawScreen(r);
  }
  if (playing_ && menu_.Active()) {
    DrawDim(r, all, 150);
    DrawPanel(r, {config_.width * 0.5f - 230.0f, config_.height * 0.5f - 190.0f, 460.0f, 380.0f});
  }
  menu_.Draw(game->window, r, config_.width, config_.height, "");
}

// The letterboxed game area in output pixels (whole pixels, the canvas texture size).
SDL_FRect GameplayFlow::SceneRect(SDL_Renderer *r) const {
  SetWindowPresentation(r, static_cast<int>(config_.width), static_cast<int>(config_.height),
                        SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_FRect rect{0, 0, config_.width, config_.height};
  SDL_GetRenderLogicalPresentationRect(r, &rect);
  SetWindowPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  rect.x = SDL_roundf(rect.x);
  rect.y = SDL_roundf(rect.y);
  rect.w = SDL_max(1.0f, SDL_roundf(rect.w));
  rect.h = SDL_max(1.0f, SDL_roundf(rect.h));
  return rect;
}

// The "game" canvas layer: the session draws in logical coordinates, scaled to
// the layer's output-pixel texture.
Kotonoha_Scene_Status GameplayFlow::DrawSceneLayer(KOTONOHA_SCENE_CALL) {
  (void)window;
  auto *self = static_cast<GameplayFlow *>(userData);
  float w = 0, h = 0;
  if (self == nullptr || target == nullptr || !SDL_GetTextureSize(target, &w, &h)) return KOTONOHA_SCENE_NULL;
  SDL_SetRenderScale(render, w / self->config_.width, h / self->config_.height);
  SDL_SetRenderDrawColor(render, 8, 9, 20, 255);
  SDL_RenderClear(render);
  self->DrawScene(render);
  SDL_SetRenderScale(render, 1.0f, 1.0f);
  return KOTONOHA_SCENE_DRAW;
}

void GameplayFlow::DrawScene(SDL_Renderer *r) {
  if (playing_) {
    session_->Draw(r);
  } else if (frontend_) {
    session_->DrawFrontend(r);
  } else if (!titleItems_.empty()) {
    session_->DrawTitle(r, titleHover_);
  } else {
    PanelStyle bg;
    bg.radius = 0;
    bg.borderWidth = 0;
    bg.top = {16, 20, 48, 255};
    bg.bottom = {4, 5, 14, 255};
    DrawPanel(r, {0, 0, config_.width, config_.height}, bg);
  }
}

SDL_AppResult GameplayFlow::RunFrame(::Kotonoha::Kotonoha *app, Kotonoha_Game *game, TTF_Font *font) {
  if (app == nullptr || game == nullptr || game->render == nullptr) return SDL_APP_FAILURE;
  SDL_Renderer *r = game->render;

  if (app->IsSplashActive()) {
    SDL_SetRenderTarget(r, nullptr);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    app->RenderSplash();
    if (game->config.enableDebugUi && !Debug::Run(app, nullptr, *game)) return SDL_APP_SUCCESS;
  } else {
    if (session_ && session_->OwnsFrontend()) {
      const SDL_AppResult status = session_->RenderFrame(app, game);
      if (status != SDL_APP_CONTINUE) return status;
    } else {
      Frame(game, font);
    }

    if (session_ && !session_->RunDebugUi(app, *game)) return SDL_APP_SUCCESS;
  }

  // Renderização comum de Overlay (FPS)
  if (game->config.showFps) {
    KotonohaFPSRender(game->window, game->render, font, nullptr);
  }

  // Serviço MCP e apresentação final
  Gameplay *mcpGameplay = (!app->IsSplashActive() && session_) ? session_->RunningGameplay() : nullptr;
  Debug::Mcp::Service(app, mcpGameplay);

  SDL_RenderPresent(r);
  return SDL_APP_CONTINUE;
}

} // namespace Kotonoha
