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

GameplayFlow::GameplayFlow(Config config) : config_(std::move(config)), menu_(config_.id) {
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
  choices_.Close();
  choicesOpen_ = false;
  playing_ = true;
  if (sync) session_->Sync();
  return true;
}

void GameplayFlow::Advance(int choice) {
  const int in = session_->Choosing() ? choice : -1;
  if (session_->Choosing() && in < 0) return;
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
  if (session_->Choosing()) {
    choices_.HandleEvent(e, game);
    return SDL_APP_CONTINUE;
  }
  if (e->type == SDL_EVENT_KEY_DOWN) {
    if (e->key.key == SDLK_ESCAPE) menu_.Show(GameMenu::Page::Pause, font_);
    else if (e->key.key == SDLK_RETURN || e->key.key == SDLK_SPACE || e->key.key == SDLK_DOWN) Advance(-1);
  } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (e->button.button == SDL_BUTTON_RIGHT) menu_.Show(GameMenu::Page::Pause, font_);
    else Advance(-1);
  }
  return SDL_APP_CONTINUE;
}

void GameplayFlow::SyncChoices() {
  const bool want = session_->Choosing() && !menu_.Active();
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

  if (playing_) {
    session_->Idle();
    session_->Sync();
    SyncChoices();
    textBox_.SetCaption(session_->Caption());
    if (session_->HasMessage()) {
      textBox_.SetMessage(session_->Speaker(), session_->Text());
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
    textBox_.Draw(r);
    if (choicesOpen_) {
      DrawDim(r, all, 130);
      const float h = 72.0f * float(session_->Choices().size()) + 36.0f;
      DrawPanel(r, {config_.width * 0.5f - 260.0f, config_.height * 0.5f - h * 0.5f, 520.0f, h});
      choices_.Draw(game->window, r);
    }
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
    Debug::Mcp::Service(app, nullptr);
    SDL_RenderPresent(r);
    return SDL_APP_CONTINUE;
  }
  if (session_ && session_->OwnsFrontend()) {
    const SDL_AppResult status = session_->RenderFrame(app, game);
    if (status != SDL_APP_CONTINUE) return status;
  } else {
    Frame(game, font);
  }
  if (session_ && !session_->RunDebugUi(app, *game)) return SDL_APP_SUCCESS;
  Debug::Mcp::Service(app, session_ ? session_->RunningGameplay() : nullptr);
  SDL_RenderPresent(r);
  return SDL_APP_CONTINUE;
}

} // namespace Kotonoha
