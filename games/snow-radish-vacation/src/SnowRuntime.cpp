#include "SnowRuntime.hpp"

#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace SnowRadish {
SnowRuntime::SnowRuntime(std::shared_ptr<SnowAssets> assets)
    : Runtime("Snow Radish Vacation!!", "0.1.0", "snow-radish-vacation"), assets_(std::move(assets)) {
  if (!assets_) throw std::invalid_argument("Snow Radish Vacation needs its DAT directory");
}

SnowRuntime::~SnowRuntime() { Shutdown(); }

void SnowRuntime::Shutdown() {
  ClearScenes();
  saves_.reset();
  backlog_.reset();
  pause_.reset();
  uniformChoice_.reset();
  mapChoice_.reset();
  textChoice_.reset();
  textBox_.reset();
  stage_.reset();
  title_.reset();
  if (session_) session_->Shutdown();
  session_.reset();
  // The provider is shared with the engine: its cached textures must go with the renderer.
  if (assets_) assets_->clearCache();
}

void SnowRuntime::initGlobal(Kotonoha::Engine &engine) {
  engine_ = &engine;
  if (session_) return;
  session_ = std::make_unique<SnowSession>(engine, assets_);
  links_.openMenu = [this] { pause_->open(); };
  links_.openBacklog = [this] { backlog_->open(); };
  links_.openLoad = [this] { OpenSaves(true); };
  links_.record = [this](const std::string &speaker, const std::string &text) { backlog_->addEntry(speaker, text); };
  title_ = std::make_unique<SnowTitleMenu>(engine, *session_, links_);
  stage_ = std::make_unique<SnowStage>(engine, *session_);
  textBox_ = std::make_unique<SnowTextBox>(engine, *session_, links_);
  textChoice_ = std::make_unique<SnowTextChoice>(engine, *session_, links_);
  mapChoice_ = std::make_unique<SnowMapChoice>(engine, *session_, links_);
  uniformChoice_ = std::make_unique<SnowUniformChoice>(engine, *session_, links_);
  pause_ = std::make_unique<SnowPauseMenu>(engine, *session_, links_);
  pause_->SetOnSave([this] { OpenSaves(false); });
  pause_->SetOnLoad([this] { OpenSaves(true); });
  backlog_ = std::make_unique<SnowBacklog>(engine, *session_);
  saves_ = std::make_unique<Kotonoha::VNSaveMenu>(engine);
}

void SnowRuntime::initMenu(Kotonoha::Engine &) {
  pause_->close();
  backlog_->close();
  AddScene(title_.get());
  session_->EnterFrontend();
}

// Draw order; input goes the other way (backlog first, stage last).
void SnowRuntime::initGameplay(Kotonoha::Engine &) {
  pause_->close();
  backlog_->close();
  backlog_->clear();
  for (Kotonoha::Scene *scene : std::initializer_list<Kotonoha::Scene *>{
           stage_.get(), textBox_.get(), textChoice_.get(), mapChoice_.get(), uniformChoice_.get(), pause_.get(),
           backlog_.get()})
    AddScene(scene);
}

void SnowRuntime::OpenSaves(bool load) {
  const auto directory = session_->SaveDirectory();
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  saves_->open(directory.string(), load);
  RemoveScene(saves_.get());
  AddScene(saves_.get());
}

SDL_FPoint SnowRuntime::sceneSize(const Kotonoha::Scene &scene) const {
  if (IsGameScene(&scene)) return {float(kGameWidth), float(kGameHeight)};
  return Runtime::sceneSize(scene);
}

bool SnowRuntime::IsGameScene(const Kotonoha::Scene *scene) const {
  return scene == title_.get() || scene == stage_.get() || scene == textBox_.get() || scene == textChoice_.get() ||
         scene == mapChoice_.get() || scene == uniformChoice_.get() || scene == pause_.get() ||
         scene == backlog_.get();
}

bool SnowRuntime::EngineSceneCaptures() const {
  for (const Kotonoha::Scene *scene : GetActiveScenes())
    if (scene && !IsGameScene(scene) && scene->capturesInput()) return true;
  return false;
}

void SnowRuntime::render(Kotonoha::RendererRef renderer, float scale) {
  if (!session_) return;
  const SDL_FRect view{0, 0, float(kViewWidth), float(kViewHeight)};
  SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(&renderer, &view);
  const auto frame = Kotonoha::UI::fit({float(kGameWidth), float(kGameHeight)}, viewport());
  session_->SetPixelScale(frame.scale * scale);
  for (Kotonoha::Scene *scene : GetActiveScenes())
    if (scene && scene->renderSpace() == Kotonoha::RenderSpace::Novel &&
        scene->renderLayer() == Kotonoha::RenderLayer::Foreground)
      renderScene(*scene, renderer, scale);
}

void SnowRuntime::update() {
  if (session_ && currentState == Kotonoha::GameState::Gameplay && !isStoryPaused()) session_->Frame();
  Runtime::update();
}

void SnowRuntime::onEvent(Kotonoha::EventRef event) {
  if (!session_) return;
  if (EngineSceneCaptures()) return Runtime::onEvent(event);
  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    const auto frame = Kotonoha::UI::fit({float(kGameWidth), float(kGameHeight)}, viewport());
    const SDL_Event game = Kotonoha::UI::toFrame(event, frame);
    session_->SetMouse(game.motion.x, game.motion.y);
  }
  Runtime::onEvent(event);
}

Kotonoha::Translation::SourceTexts SnowRuntime::translationSources() const {
  return session_ ? session_->TranslationSources() : Kotonoha::Translation::SourceTexts{};
}

// "start.obj | route N | pc 0x..": route N is the script id used by the translations.
std::string SnowRuntime::debugNode() const {
  if (!session_ || !session_->Playing() || !session_->Story()) return {};
  auto &vm = session_->Story()->vm();
  const auto object = vm.current();
  if (!object) return session_->Story()->currentScript();
  char pc[16];
  std::snprintf(pc, sizeof pc, "0x%04x", unsigned(vm.pc()));
  return object->name + " | route " + std::to_string(object->id & 0xfff) + " | pc " + pc;
}

void SnowRuntime::saveGame(const std::string &path) {
  if (!session_ || !session_->Playing()) throw std::logic_error("No story is being played");
  Kotonoha::VNSave save;
  save.nodeId = 0; // the VM state lives in gameData; there is no engine script node
  save.gameData = session_->Serialize();
  save.write(path);
}

std::vector<Kotonoha::DebugNode> SnowRuntime::debugNodes() const {
  std::vector<Kotonoha::DebugNode> result;
  if (!session_ || !session_->Story()) return result;
  const auto &story = *session_->Story();
  for (const auto &name : story.debugScripts()) {
    Kotonoha::DebugNode node{name, name, {"script"}};
    if (story.currentScript() == name) {
      if (!story.background().empty())
        node.assets.push_back("graphic/" + story.background() + ".evd");
      for (const auto &sprite : story.sprites())
        node.assets.push_back("graphic/" + sprite.name + ".chd");
      if (!story.message().voice.empty())
        node.assets.push_back("voice/" + story.message().voice);
    }
    result.push_back(std::move(node));
  }
  return result;
}

void SnowRuntime::debugJumpToNode(const std::string &id) {
  if (!session_) throw std::logic_error("Game is not initialized");
  session_->DebugJump(id);
}

void SnowRuntime::loadGame(const std::string &path) {
  if (!session_ || !engine_) throw std::logic_error("Snow Radish Vacation is not initialised");
  const auto save = Kotonoha::VNSave::read(path);
  if (!session_->Restore(save.gameData)) throw std::runtime_error("Cannot load " + path);
  SetState(Kotonoha::GameState::Gameplay);
  init(*engine_);
}

} // namespace SnowRadish
