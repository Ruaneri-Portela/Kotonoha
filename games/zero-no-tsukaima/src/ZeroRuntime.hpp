#pragma once

#include "ZeroComponents.hpp"

#include <Kotonoha/core/Runtime.hpp>

#include <memory>

namespace ZeroNoTsukaima {

// Zero no Tsukaima as a Kotonoha game. Menu state = the retail front end
// (ZeroTitleMenu); Gameplay state = the story drawn by the polymorphic engine
// components. The viewport is 960x672 (640x448 x1.5); game scenes work in
// 640x448 units (sceneSize) and the engine fits them, and engine components,
// into the viewport.
class ZeroRuntime : public Kotonoha::Runtime {
public:
  explicit ZeroRuntime(std::shared_ptr<DiscAssetProvider> disc);
  ~ZeroRuntime() override;

  // Releases session textures and audio; call before the Engine is destroyed.
  void Shutdown();
  ZeroSession *Session() { return session_.get(); }

  void initGlobal(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;

  SDL_Point viewport() const override { return {kViewWidth, kViewHeight}; }
  SDL_FPoint sceneSize(const Kotonoha::Scene &scene) const override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
  void update() override;
  void onEvent(Kotonoha::EventRef event) override;

  // Route = SCENE_ID index of each KS script.
  Kotonoha::Translation::SourceTexts translationSources() const override;
  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &id) override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  static constexpr int kViewWidth = kGameWidth * 3 / 2, kViewHeight = kGameHeight * 3 / 2;

private:

  std::shared_ptr<DiscAssetProvider> disc_;
  Kotonoha::Engine *engine_ = nullptr;
  std::unique_ptr<ZeroSession> session_;
  std::unique_ptr<ZeroTitleMenu> title_;
  std::unique_ptr<ZeroStage> stage_;
  std::unique_ptr<ZeroTextBox> textBox_;
  std::unique_ptr<ZeroSelectChoice> select_;
  std::unique_ptr<ZeroMoveChoice> move_;
  std::unique_ptr<ZeroRoulette> roulette_;
  std::unique_ptr<ZeroCommandMenu> command_;
  std::unique_ptr<ZeroBacklog> backlog_;
};

} // namespace ZeroNoTsukaima
