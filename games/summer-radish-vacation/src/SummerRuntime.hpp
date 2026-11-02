#pragma once
#include "SummerAssets.hpp"
#include <Kotonoha/core/Runtime.hpp>
namespace SummerRadish {
class SummerRuntime final : public Kotonoha::Runtime {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  explicit SummerRuntime(std::shared_ptr<SummerAssets>);
  ~SummerRuntime() override;
  void shutdown();
  void initMenu(Kotonoha::Engine &) override;
  void initGameplay(Kotonoha::Engine &) override;
  void update() override;
  void onEvent(Kotonoha::EventRef) override;
  void saveGame(const std::string &) override;
  void loadGame(const std::string &) override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  int translationRoute()const override;
  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &) override;
  SDL_Point viewport() const override { return {960, 720}; }
  SDL_FPoint sceneSize(const Kotonoha::Scene &) const override;
};
} // namespace SummerRadish
