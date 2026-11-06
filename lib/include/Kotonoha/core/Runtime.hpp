#pragma once

#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/localization/Translation.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kotonoha {

class Engine;
class SplashScreen;
class SplashChainloader;

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
  std::unique_ptr<SplashChainloader> splash;
  Engine *splashEngine = nullptr;

public:
  Runtime(std::string name, std::string version, std::string identify);
  virtual ~Runtime();
  // Native script menus may poll pointer-down and pointer-up on different
  // frames.
  virtual bool continuousTouch() const { return false; }

  // Ciclo de inicialização
  virtual void init(Engine &engine);
  virtual void initGlobal(Engine &engine);
  // Default: the engine splash (splashPictures()); when it ends the runtime
  // switches to GameState::Menu.
  virtual void initSplash(Engine &engine);
  // Splash media shown by the default splash. The first matching asset among
  // SPLASH.MP4, SPLASH.MOV, SPLASH.WEBM, and SPLASH.PNG is used unless
  // overridden. Videos play their embedded audio when present.
  virtual std::vector<std::filesystem::path> splashPictures() const;
  virtual void initMenu(Engine &engine);
  virtual void initGameplay(Engine &engine);

  // Ciclos de renderização divididos
  virtual void render(RendererRef renderer, float scale);
  virtual void renderSplash(RendererRef renderer, float scale);
  virtual void renderMenu(RendererRef renderer, float scale);
  virtual void renderGameplay(RendererRef renderer, float scale);

  virtual void update();
  virtual bool onEvent(EventRef event);
  virtual bool onScreenEvent(EventRef event);
  virtual std::string diagnostic() const { return {}; }
  virtual bool handleNewGameAction() { return false; }
  virtual bool handleAdvanceAction() { return false; }
  virtual bool advanceStoryTime(double) { return false; }
  virtual bool handleBackAction() { return false; }
  virtual bool handleBacklogAction() { return false; }
  virtual void renderScreen(RendererRef renderer, float scale);
  virtual void renderBackground(RendererRef renderer, float scale);
  // Games with scene transactions can retain the fully composed Novel raster
  // in an engine-owned target. Readiness means a primary image/video drew.
  virtual bool useOwnedNarrativeTarget() const { return false; }
  // Optional game-authored vertical presentation inside the normal Novel
  // canvas. 1.0 fills the fitted canvas; e.g. 0.75 keeps the whole frame but
  // compresses it into the centered 75%, leaving black bands above/below.
  // The physical window and generic engine Settings UI remain unchanged.
  virtual float narrativePresentationHeightScale() const { return 1.0f; }
  virtual bool narrativeFrameDrawable() const { return false; }
  virtual bool narrativeBoundaryFrame() const { return false; }
  virtual std::string narrativeVisualOwner() const { return {}; }
  // Called on the Novel target after an outgoing frame has been composed.
  // Native/modal overlays stay above a retained narrative surface.
  virtual void renderNarrativeOverlay(RendererRef, float) {}
  virtual bool devPauseRequested() const { return false; }
  virtual bool devConsumeStep() { return false; }
  virtual void devIdle() {}
  virtual void devControlPump(RendererRef) {}
  virtual void devInputProcessed(EventRef) {}
  // Novel-space UI, drawn after story content and forced subtitles.
  virtual void renderOverlay(RendererRef renderer, float scale);
  virtual void saveGame(const std::string &path);
  virtual void loadGame(const std::string &path);

  // Game texts by translation route (the Translation section id), for
  // Engine::dumpTranslation. Games with their own script formats return their
  // texts here and look them up with Engine::GetTranslation().text(route, ...).
  virtual Translation::SourceTexts translationSources() const { return {}; }

  virtual int translationRoute() const;
  // Original keys currently presented by a native script runtime. Used to
  // follow the exact dialogue/choice in the live translation editor.
  virtual std::vector<std::string> activeTranslationKeys() const { return {}; }
  virtual std::vector<int> translationRoutes() const { return {}; }
  virtual std::vector<std::string> translationTexts(int route) const {
    auto sources = translationSources();
    auto found = sources.find(route);
    return found == sources.end() ? std::vector<std::string>{}
                                  : std::move(found->second);
  }

  // Current story position of games with their own interpreter (script,
  // label, route...), shown as the node by the debug overlay and MCP. Empty
  // means the engine VNManager node is used. The engine's event clock resets
  // whenever this position changes.
  virtual std::string debugNode() const { return {}; }
  // Optional stable identity for the shared node clock. Native interpreters
  // can keep one clock across multiple instruction positions in a node; an
  // empty key uses debugNode() as the clock identity.
  virtual std::string nodeClockKey() const { return {}; }
  // Timed native scripts expose their own node clock. Standard interpreters
  // use the engine clock, keyed by debugNode() or nodeClockKey().
  virtual bool usesTimedNodeClock() const { return false; }
  virtual std::optional<double> debugNodeSeconds() const {
    return std::nullopt;
  }
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

  // Engine-wide pause, including debug, settings, MCP and active scene modals.
  bool isStoryPaused() const;
  bool hasPausingScene() const {
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
