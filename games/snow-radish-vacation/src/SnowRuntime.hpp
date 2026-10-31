#pragma once

#include "SnowComponents.hpp"

#include <Kotonoha/core/Runtime.hpp>

#include <memory>

namespace Kotonoha {
class VNSaveMenu;
}

namespace SnowRadish {

// Snow Radish Vacation!! as a Kotonoha game. Menu state = logo / title
// (SnowTitleMenu); Gameplay state = the story drawn by the polymorphic engine
// components. The viewport is 960x720 (800x600 x1.2); game scenes work in
// 800x600 units (sceneSize) and engine scenes (the save browser and its
// dialogs) in the 1280x720 design space, both fitted into the viewport.
class SnowRuntime : public Kotonoha::Runtime {
public:
  explicit SnowRuntime(std::shared_ptr<SnowAssets> assets);
  ~SnowRuntime() override;

  // Releases session textures and audio; call before the Engine is destroyed.
  void Shutdown();
  SnowSession *Session() { return session_.get(); }

  void initGlobal(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;

  SDL_Point viewport() const override { return {kViewWidth, kViewHeight}; }
  SDL_FPoint sceneSize(const Kotonoha::Scene &scene) const override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
  void update() override;
  void onEvent(Kotonoha::EventRef event) override;

  // Route = 12-bit script object id (texts in Shift-JIS converted to UTF-8).
  Kotonoha::Translation::SourceTexts translationSources() const override;
  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &id) override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  static constexpr int kViewWidth = kGameWidth * 6 / 5, kViewHeight = kGameHeight * 6 / 5;

private:
  bool IsGameScene(const Kotonoha::Scene *scene) const;
  bool EngineSceneCaptures() const;
  void OpenSaves(bool load);

  std::shared_ptr<SnowAssets> assets_;
  Kotonoha::Engine *engine_ = nullptr;
  SnowLinks links_;
  std::unique_ptr<SnowSession> session_;
  std::unique_ptr<SnowTitleMenu> title_;
  std::unique_ptr<SnowStage> stage_;
  std::unique_ptr<SnowTextBox> textBox_;
  std::unique_ptr<SnowTextChoice> textChoice_;
  std::unique_ptr<SnowMapChoice> mapChoice_;
  std::unique_ptr<SnowUniformChoice> uniformChoice_;
  std::unique_ptr<SnowPauseMenu> pause_;
  std::unique_ptr<SnowBacklog> backlog_;
  std::unique_ptr<Kotonoha::VNSaveMenu> saves_;
};

} // namespace SnowRadish
