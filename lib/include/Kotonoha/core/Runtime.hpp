#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/localization/Translation.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Kotonoha {

class Engine;
class SplashScreen;

enum class GameState { Splash, Menu, Gameplay };

struct DebugNode {
  std::string id;
  std::string label;
  std::vector<std::string> assets;
};

class Runtime {
public:
  const std::string name;
  const std::string version;
  const std::string identify;

protected:
  GameState currentState = GameState::Splash;

  // Vetores de cenas divididos por estado
  std::vector<Scene *> splashScenes;
  std::vector<Scene *> menuScenes;
  std::vector<Scene *> gameplayScenes;

private:
  std::unique_ptr<SplashScreen> splash;
  Engine *splashEngine = nullptr;

public:
  Runtime(std::string name, std::string version, std::string identify);
  virtual ~Runtime();

  // Ciclo de inicialização
  virtual void init(Engine &engine);
  virtual void initGlobal(Engine &engine);
  // Default: the engine splash (splashPictures()); when it ends the runtime
  // switches to GameState::Menu.
  virtual void initSplash(Engine &engine);
  // Pictures shown by the default splash; the engine logo
  // (assets/SPLASH.PNG in the executable resources) unless overridden.
  virtual std::vector<std::filesystem::path> splashPictures() const;
  virtual void initMenu(Engine &engine);
  virtual void initGameplay(Engine &engine);

  // Ciclos de renderização divididos
  virtual void render(RendererRef renderer, float scale);
  virtual void renderSplash(RendererRef renderer, float scale);
  virtual void renderMenu(RendererRef renderer, float scale);
  virtual void renderGameplay(RendererRef renderer, float scale);

  virtual void update();
  virtual void onEvent(EventRef event);
  virtual bool onScreenEvent(EventRef event);
  virtual void renderScreen(RendererRef renderer, float scale);
  virtual void renderBackground(RendererRef renderer, float scale);
  virtual void saveGame(const std::string &path);
  virtual void loadGame(const std::string &path);

  // Game texts by translation route (the Translation section id), for
  // Engine::dumpTranslation. Games with their own script formats return their
  // texts here and look them up with Engine::GetTranslation().text(route, ...).
  virtual Translation::SourceTexts translationSources() const { return {}; }

  // Current story position of games with their own interpreter (script,
  // label, route...), shown as the node by the debug overlay and MCP. Empty
  // means the engine VNManager node is used.
  virtual std::string debugNode() const { return {}; }
  virtual std::optional<double> debugNodeSeconds() const { return std::nullopt; }
  virtual std::vector<DebugNode> debugNodes() const { return {}; }
  virtual void debugJumpToNode(const std::string &) {
    throw std::runtime_error("This runtime does not support node jumps");
  }
  virtual bool hasCustomKtrf() const { return false; }
  virtual void loadKtrf(const std::string &) {
    throw std::runtime_error("This runtime does not support custom KTRF");
  }
  virtual void dumpKtrf(const std::string &) const {
    throw std::runtime_error("This runtime does not support custom KTRF");
  }

  // Size of the logical render area (novel space) for this game. Engine
  // components are fitted into it from the 1280x720 design space.
  virtual SDL_Point viewport() const { return {1280, 720}; }
  // Logical size a Novel scene draws in; it is fitted into viewport().
  // Default: the 1280x720 design space for engine components, the viewport
  // itself for the rest. Games override it for their native resolution.
  virtual SDL_FPoint sceneSize(const Scene &scene) const;
  // Renders / dispatches one Novel scene inside its sceneSize() frame.
  void renderScene(Scene &scene, RendererRef renderer, float scale) const;
  bool dispatchEvent(Scene &scene, EventRef event) const;

  GameState GetState() const { return currentState; }
  void SetState(GameState newState) { currentState = newState; }

  // Adiciona a cena ao vetor correspondente ao estado especificado
  void AddScene(Scene *scene, GameState state) {
    if (!scene)
      return;
    switch (state) {
    case GameState::Splash:
      splashScenes.push_back(scene);
      break;
    case GameState::Menu:
      menuScenes.push_back(scene);
      break;
    case GameState::Gameplay:
      gameplayScenes.push_back(scene);
      break;
    }
  }

  // Adiciona a cena ao vetor do estado atual (ou a todos no initGlobal)
  void AddScene(Scene *scene) { AddScene(scene, currentState); }

  // Registra uma cena de jogo em todos os estados.
  void AddGlobalScene(Scene *scene) {
    if (!scene)
      return;
    splashScenes.push_back(scene);
    menuScenes.push_back(scene);
    gameplayScenes.push_back(scene);
  }

  void ClearScenes() {
    splashScenes.clear();
    menuScenes.clear();
    gameplayScenes.clear();
  }

  const std::vector<Scene *> &GetScenes(GameState state) const {
    switch (state) {
    case GameState::Splash:
      return splashScenes;
    case GameState::Menu:
      return menuScenes;
    case GameState::Gameplay:
      return gameplayScenes;
    }
    throw std::invalid_argument("Unknown game state");
  }

  const std::vector<Scene *> &GetActiveScenes() const {
    return GetScenes(currentState);
  }

  bool isStoryPaused() const {
    for (const auto *scene : GetActiveScenes())
      if (scene && scene->pausesStory())
        return true;
    return false;
  }

  void RemoveScene(Scene *scene) {
    if (!scene)
      return;

    auto remove_from = [scene](std::vector<Scene *> &scenes) {
      scenes.erase(std::remove(scenes.begin(), scenes.end(), scene),
                   scenes.end());
    };

    remove_from(splashScenes);
    remove_from(menuScenes);
    remove_from(gameplayScenes);
  }
};

} // namespace Kotonoha