#include <Kotonoha/componentes/SplashScreen.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <stdexcept>

namespace Kotonoha {

Runtime::Runtime(std::string name, std::string version, std::string identify)
    : name(std::move(name)), version(std::move(version)),
      identify(std::move(identify)) {}

Runtime::~Runtime() = default;

void Runtime::saveGame(const std::string &) {
  throw std::logic_error("This game has not implemented saveGame");
}

void Runtime::loadGame(const std::string &) {
  throw std::logic_error("This game has not implemented loadGame");
}

void Runtime::init(Engine &engine) {
  engine.GetTimeline().stop();
  if (SDL_TextInputActive(&engine.GetWindow()) &&
      !SDL_StopTextInput(&engine.GetWindow()))
    throw std::runtime_error(SDL_GetError());
  ClearScenes();

  // Executa inicialização global primeiro
  initGlobal(engine);

  // Inicializa o estado atual
  switch (currentState) {
  case GameState::Splash:
    initSplash(engine);
    break;
  case GameState::Menu:
    initMenu(engine);
    break;
  case GameState::Gameplay:
    initGameplay(engine);
    break;
  }
}

SDL_FPoint Runtime::sceneSize(const Scene &scene) const {
  if (scene.designSpace())
    return {UI::width, UI::height};
  const SDL_Point view = viewport();
  return {static_cast<float>(view.x), static_cast<float>(view.y)};
}

void Runtime::renderScene(Scene &scene, RendererRef renderer, float scale) const {
  const auto frame = UI::fit(sceneSize(scene), viewport());
  UI::FrameScope scope(renderer, frame);
  scene.render(renderer, scale * frame.scale);
}

bool Runtime::dispatchEvent(Scene &scene, EventRef event) const {
  return scene.handleEvent(
      UI::toFrame(event, UI::fit(sceneSize(scene), viewport())));
}

void Runtime::initGlobal(Engine &engine) {}
std::vector<std::filesystem::path> Runtime::splashPictures() const {
  if (auto splash = FileResolver::findEngineAsset("SPLASH.PNG"))
    return {*splash};
  return {};
}

void Runtime::initSplash(Engine &engine) {
  splashEngine = &engine;
  if (!splash)
    splash = std::make_unique<SplashScreen>(engine, splashPictures());
  else
    splash->restart();
  AddScene(splash.get(), GameState::Splash);
}
void Runtime::initMenu(Engine &engine) {}
void Runtime::initGameplay(Engine &engine) {}

void Runtime::render(RendererRef renderer, float scale) {
  switch (currentState) {
  case GameState::Splash:
    renderSplash(renderer, scale);
    break;
  case GameState::Menu:
    renderMenu(renderer, scale);
    break;
  case GameState::Gameplay:
    renderGameplay(renderer, scale);
    break;
  }
}

void Runtime::renderSplash(RendererRef renderer, float scale) {
  for (Scene *scene : splashScenes) {
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Foreground) {
      renderScene(*scene, renderer, scale);
    }
  }
}

void Runtime::renderMenu(RendererRef renderer, float scale) {
  for (Scene *scene : menuScenes) {
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Foreground) {
      renderScene(*scene, renderer, scale);
    }
  }
}

void Runtime::renderGameplay(RendererRef renderer, float scale) {
  for (Scene *scene : gameplayScenes) {
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Foreground) {
      renderScene(*scene, renderer, scale);
    }
  }
}

void Runtime::update() {
  if (currentState == GameState::Splash && splash && splashEngine &&
      splash->finished()) {
    SetState(GameState::Menu);
    init(*splashEngine);
    return;
  }
  const auto activeScenes = GetActiveScenes();
  for (Scene *scene : activeScenes) {
    const auto &current = GetActiveScenes();
    if (scene &&
        std::find(current.begin(), current.end(), scene) != current.end()) {
      scene->update();
    }
  }
}

void Runtime::onEvent(EventRef event) {
  const auto activeScenes = GetActiveScenes();
  for (auto it = activeScenes.rbegin(); it != activeScenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Novel &&
        (*it)->capturesInput()) {
      dispatchEvent(**it, event);
      return;
    }
  }
  const auto state = currentState;
  for (auto it = activeScenes.rbegin(); it != activeScenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Novel &&
        dispatchEvent(**it, event))
      return;
    if (state != currentState)
      return;
    if (isStoryPaused())
      return;
  }
}

bool Runtime::onScreenEvent(EventRef event) {
  const auto scenes = GetActiveScenes();
  for (auto it = scenes.rbegin(); it != scenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Screen &&
        (*it)->capturesInput()) {
      (*it)->handleEvent(event);
      return true;
    }
  }
  const auto state = currentState;
  for (auto it = scenes.rbegin(); it != scenes.rend(); ++it) {
    if (*it && (*it)->renderSpace() == RenderSpace::Screen &&
        (*it)->handleEvent(event))
      return true;
    if (state != currentState)
      return true;
  }
  return false;
}

void Runtime::renderScreen(RendererRef renderer, float scale) {
  for (auto *scene : GetActiveScenes())
    if (scene && scene->renderSpace() == RenderSpace::Screen)
      scene->render(renderer, scale);
}

void Runtime::renderBackground(RendererRef renderer, float scale) {
  for (auto *scene : GetActiveScenes())
    if (scene && scene->renderSpace() == RenderSpace::Novel &&
        scene->renderLayer() == RenderLayer::Background)
      renderScene(*scene, renderer, scale);
}

} // namespace Kotonoha