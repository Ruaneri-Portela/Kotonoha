#pragma once

#include "DdlcAssets.hpp"

#include <Kotonoha/core/Runtime.hpp>

#include <memory>

namespace Ddlc {

class GameSession;

class GameRuntime final : public Kotonoha::Runtime {
public:
  explicit GameRuntime(std::shared_ptr<GameAssets> assets);
  ~GameRuntime() override;

  void shutdown();
  void saveGame(const std::string&path)override;
  void loadGame(const std::string&path)override;
  GameSession *session() { return session_.get(); }

  void initGlobal(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;
  void update() override;
  bool onEvent(Kotonoha::EventRef event) override;
  bool handleBackAction() override;
  bool handleBacklogAction() override;
  int translationRoute() const override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
  SDL_Point viewport() const override { return {1280, 720}; }
  Kotonoha::Translation::SourceTexts translationSources() const override;
  std::string debugNode() const override;
  std::string nodeClockKey() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &id) override;

private:
  void addGameScenes();
  std::shared_ptr<GameAssets> assets_;
  Kotonoha::Engine *engine_ = nullptr;
  std::unique_ptr<GameSession> session_;
  std::unique_ptr<Kotonoha::Scene> stage_, menu_, dialogue_, choices_, pause_;
};

} // namespace Ddlc
