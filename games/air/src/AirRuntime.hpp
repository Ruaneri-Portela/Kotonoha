#pragma once
#include "AirAssets.hpp"
#include <Kotonoha/core/Runtime.hpp>
namespace Air {
class AirRuntime final : public Kotonoha::Runtime {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  explicit AirRuntime(std::shared_ptr<AirAssets>, int scene = -1);
  ~AirRuntime() override;
  void shutdown();
  bool continuousTouch() const override { return true; }
  std::string diagnostic() const override;
  void initMenu(Kotonoha::Engine &) override;
  void initGameplay(Kotonoha::Engine &) override;
  void update() override;
  bool onEvent(Kotonoha::EventRef) override;
  void saveGame(const std::string &) override;
  void loadGame(const std::string &) override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  int translationRoute() const override;
  std::vector<int> translationRoutes() const override;
  std::vector<std::string> translationTexts(int route) const override;
  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &) override;
  SDL_Point viewport() const override { return {960, 720}; }
  SDL_FPoint sceneSize(const Kotonoha::Scene &) const override;
};
} // namespace Air
