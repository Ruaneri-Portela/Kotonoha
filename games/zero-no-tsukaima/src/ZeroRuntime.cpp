#include "ZeroRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <algorithm>
#include <stdexcept>

namespace ZeroNoTsukaima {
ZeroRuntime::ZeroRuntime(std::shared_ptr<DiscAssetProvider> disc)
    : Runtime("Zero no Tsukaima", "0.1.0",
              "me.hirameki.kotonoha.zeronotsukaima"),
      disc_(std::move(disc)) {
  if (!disc_)
    throw std::invalid_argument("Zero no Tsukaima needs a disc image");
}

ZeroRuntime::~ZeroRuntime() { Shutdown(); }

void ZeroRuntime::Shutdown() {
  ClearScenes();
  backlog_.reset();
  command_.reset();
  roulette_.reset();
  move_.reset();
  select_.reset();
  textBox_.reset();
  stage_.reset();
  title_.reset();
  if (session_)
    session_->Shutdown();
  session_.reset();
  // The provider is shared with the engine: its cached textures must go with
  // the renderer.
  if (disc_)
    disc_->clearCache();
}

void ZeroRuntime::initGlobal(Kotonoha::Engine &engine) {
  engine_ = &engine;
  if (session_)
    return;
  session_ = std::make_unique<ZeroSession>(engine, disc_);
  title_ = std::make_unique<ZeroTitleMenu>(engine, *session_);
  stage_ = std::make_unique<ZeroStage>(engine, *session_);
  textBox_ = std::make_unique<ZeroTextBox>(engine, *session_);
  select_ = std::make_unique<ZeroSelectChoice>(engine, *session_);
  move_ = std::make_unique<ZeroMoveChoice>(engine, *session_);
  roulette_ = std::make_unique<ZeroRoulette>(engine, *session_);
  command_ = std::make_unique<ZeroCommandMenu>(engine, *session_);
  backlog_ = std::make_unique<ZeroBacklog>(engine, *session_);
}

void ZeroRuntime::initMenu(Kotonoha::Engine &) {
  AddScene(title_.get());
  session_->EnterFrontend();
}

// Draw order; input goes the other way (backlog first, stage last).
void ZeroRuntime::initGameplay(Kotonoha::Engine &) {
  for (Kotonoha::Scene *scene : std::initializer_list<Kotonoha::Scene *>{
           stage_.get(), textBox_.get(), select_.get(), move_.get(),
           roulette_.get(), command_.get(), backlog_.get()})
    AddScene(scene);
}

SDL_FPoint ZeroRuntime::sceneSize(const Kotonoha::Scene &scene) const {
  if (scene.designSpace())
    return Runtime::sceneSize(scene);
  return {float(kGameWidth), float(kGameHeight)};
}

void ZeroRuntime::render(Kotonoha::RendererRef renderer, float scale) {
  if (!session_)
    return;
  const SDL_FRect view{0, 0, float(kViewWidth), float(kViewHeight)};
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(&renderer, &view);
  const auto frame =
      Kotonoha::UI::fit({float(kGameWidth), float(kGameHeight)}, viewport());
  session_->SetPixelScale(frame.scale * scale);
  Runtime::render(renderer, scale);
}

void ZeroRuntime::update() {
  if (session_ && currentState == Kotonoha::GameState::Gameplay)
    session_->Frame();
  Runtime::update();
}

bool ZeroRuntime::onEvent(Kotonoha::EventRef event) {
  if (!session_)
    return false;
  // Menu, map choices and roulette keep navigation controls; dialogue-only
  // shortcuts must never turn Cancel into a held skip inside those screens.
  if (textBox_ && (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                   event.type == SDL_EVENT_GAMEPAD_REMOVED ||
                   (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP &&
                    event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)))
    textBox_->cancelHeldSkip();
  // Shared save/name dialogs own their original controller events (including
  // rename/delete shortcuts); only this game's retail scenes use its mapping.
  for (auto *scene : GetActiveScenes())
    if (scene && scene->capturesInput() && scene != select_.get() &&
        scene != move_.get() && scene != roulette_.get() &&
        scene != command_.get() && scene != backlog_.get()) {
      if (textBox_)
        textBox_->cancelHeldSkip();
      return Runtime::onEvent(event);
    }
  const bool dialogue = currentState == Kotonoha::GameState::Gameplay &&
                        session_->Playing() && !session_->ScreenActive() &&
                        !session_->Choosing() && !session_->RouletteActive() &&
                        !engine_->isStoryPaused();
  auto mapped =
      Kotonoha::MapControllerEvent(event, dialogue, session_->HistoryActive());
  if (mapped.type == SDL_EVENT_KEY_DOWN && !mapped.key.repeat) {
    switch (mapped.key.key) {
    case SDLK_F2:
      session_->StepRate(false, -1);
      return true;
    case SDLK_F3:
      session_->StepRate(false, +1);
      return true;
    case SDLK_F4:
      session_->StepRate(true, -1);
      return true;
    case SDLK_F5:
      session_->StepRate(true, +1);
      return true;
    default:
      break;
    }
  }
  return Runtime::onEvent(mapped);
}

std::vector<int> ZeroRuntime::translationRoutes() const {
  std::vector<int> ids;
  for (size_t n = 0; n < disc_->Source().Names().size(); ++n)
    ids.push_back(int(n));
  return ids;
}
std::vector<std::string> ZeroRuntime::translationTexts(int route) const {
  const auto &source = disc_->Source();
  if (route < 0 || size_t(route) >= source.Names().size())
    return {};
  const auto bytes = disc_->read("script/" + source.Names()[route]);
  std::vector<SceneTag> tags;
  return ParseSceneTags(bytes, &tags) ? Game::SceneTexts(tags)
                                      : std::vector<std::string>{};
}
Kotonoha::Translation::SourceTexts ZeroRuntime::translationSources() const {
  Kotonoha::Translation::SourceTexts texts;
  const SceneSource &source = disc_->Source();
  for (size_t i = 0; i < source.Names().size(); ++i) {
    std::vector<uint8_t> bytes;
    std::vector<SceneTag> tags;
    if (!source.LoadScene(i, &bytes) || !ParseSceneTags(bytes, &tags))
      continue;
    auto scene = Game::SceneTexts(tags);
    if (!scene.empty())
      texts[int(i)] = std::move(scene);
  }
  return texts;
}

// "SCRIPT.KS *label | scene N | tag M": scene N is the translation route.
std::string ZeroRuntime::debugNode() const {
  if (!session_ || !session_->Playing())
    return {};
  const auto *game = session_->Story();
  std::string text = game->ScriptName();
  if (const auto label = game->CurrentLabel(); !label.empty())
    text += ' ' + label;
  return text + " | scene " + std::to_string(game->SceneIndex()) + " | tag " +
         std::to_string(game->Pc());
}
std::string ZeroRuntime::nodeClockKey() const {
  if (!session_ || !session_->Playing())
    return {};
  // All positions within the same script share the node clock; only the
  // event clock resets between scenes/tags of a script.
  return session_->Story()->ScriptName();
}

void ZeroRuntime::saveGame(const std::string &path) {
  if (!session_ || !session_->Playing())
    throw std::logic_error("No story is being played");
  if (!session_->SaveTo(path))
    throw std::runtime_error("Cannot save to " + path);
}

std::vector<Kotonoha::DebugNode> ZeroRuntime::debugNodes() const {
  std::vector<Kotonoha::DebugNode> result;
  for (const auto &name : disc_->Source().Names()) {
    Kotonoha::DebugNode node{name, name, {"script/" + name}};
    if (session_ && session_->Story() &&
        session_->Story()->ScriptName() == name) {
      const auto &story = *session_->Story();
      if (story.Voice() > 0)
        node.assets.push_back("voice/" + std::to_string(story.Voice()));
      if (!story.Bgm().empty())
        node.assets.push_back("sound/" + story.Bgm());
      if (story.Movie() >= 0)
        node.assets.push_back("movie/" + std::to_string(story.Movie()));
      const auto &source = disc_->Source();
      const auto &catalog = source.Catalog();
      for (const auto &element : story.Scene()) {
        int index = source.ScenedatIndex(element.image);
        if (element.kind == Game::Element::Stage) {
          const auto special = catalog.SpecialStage(element.image);
          index = special.empty() ? catalog.ResolveStage(element.image)
                                  : source.ScenedatIndex(special);
        } else if (element.kind == Game::Element::Event) {
          index = catalog.ResolveEvent(element.image);
        } else if (element.kind == Game::Element::Character) {
          index = catalog.ResolveSprite(element.image);
        }
        if (index >= 0)
          node.assets.push_back("scenedat/" + std::to_string(index));
      }
    }
    result.push_back(std::move(node));
  }
  return result;
}

void ZeroRuntime::debugJumpToNode(const std::string &id) {
  if (!session_)
    throw std::logic_error("Game is not initialized");
  session_->DebugJump(id);
}

void ZeroRuntime::loadGame(const std::string &path) {
  if (!session_ || !engine_)
    throw std::logic_error("Zero no Tsukaima is not initialised");
  if (!session_->LoadFrom(path))
    throw std::runtime_error("Cannot load " + path);
  SetState(Kotonoha::GameState::Gameplay);
  init(*engine_);
}

} // namespace ZeroNoTsukaima
