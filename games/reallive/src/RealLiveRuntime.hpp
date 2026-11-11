#pragma once
#include <Kotonoha/core/Runtime.hpp>
#include <RealLive/RealLiveAssets.hpp>
#include <cstdint>
namespace RealLive {
class RealLiveRuntime final : public Kotonoha::Runtime {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  struct RenderStats {
    uint64_t filteredPatterns = 0;
    uint64_t filteredDCFrames = 0;
    uint64_t filteredSceneFrames = 0;
    uint64_t textureUploads = 0;
    uint64_t originalImageUploads = 0;
    uint64_t gpuFrames = 0;
    uint64_t softwareEffectFrames = 0;
  };
  explicit RealLiveRuntime(std::shared_ptr<Kotonoha::Providers::RealLiveAssets>,
                           int scene = -1);
  ~RealLiveRuntime() override;
  void shutdown();
  bool continuousTouch() const override { return true; }
  std::string diagnostic() const override;
  RenderStats renderStats() const;
  void initMenu(Kotonoha::Engine &) override;
  void initGameplay(Kotonoha::Engine &) override;
  void update() override;
  bool onEvent(Kotonoha::EventRef) override;
  void saveGame(const std::string &) override;
  void loadGame(const std::string &) override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  int translationRoute() const override;
  std::string nodeClockKey() const override;
  std::vector<int> translationRoutes() const override;
  std::vector<std::string> translationTexts(int route) const override;
  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  std::vector<Kotonoha::DebugAudioAsset> debugSceneAudio() override;
  std::optional<Kotonoha::AudioClip>
  debugOriginalAudio(const std::string &) override;
  void debugJumpToNode(const std::string &) override;
  void debugRestartScene() override;
  bool debugStep();
  SDL_Point viewport() const override { return {960, 720}; }
  SDL_FPoint sceneSize(const Kotonoha::Scene &) const override;
};
} // namespace RealLive
