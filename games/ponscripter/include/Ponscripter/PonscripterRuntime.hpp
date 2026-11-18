// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/core/Runtime.hpp>
#include <Ponscripter/PonscripterAssets.hpp>
#include <Ponscripter/PonscripterVm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Ponscripter {

class PonscripterRuntime : public Kotonoha::Runtime {
public:
  explicit PonscripterRuntime(
      std::shared_ptr<PonscripterAssets> assets = nullptr);
  ~PonscripterRuntime() override;
  void shutdown();

  SDL_Point viewport() const override;
  bool continuousTouch() const override { return true; }

  void init(Kotonoha::Engine &engine) override;
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;

  void update() override;
  void renderGameplay(Kotonoha::RendererRef renderer, float scale) override;
  bool onEvent(Kotonoha::EventRef event) override;

  bool handleAdvanceAction() override;
  bool advanceStoryTime(double seconds) override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  void debugJumpToNode(const std::string &id) override;
  void debugRestartScene() override;
  std::string dialogueText() const override;
  std::string dialogueSpeaker() const override;

  int translationRoute() const override;
  std::vector<int> translationRoutes() const override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  std::vector<std::string> activeTranslationKeys() const override;
  std::vector<Kotonoha::DebugAudioAsset> debugSceneAudio() override;
  std::optional<Kotonoha::AudioClip>
  debugOriginalAudio(const std::string &id) override;
  void debugOverlayAssetChanged(const std::string &id) override;

  PonscripterVm &vm() { return vm_; }
  const PonscripterVm &vm() const { return vm_; }

  std::shared_ptr<PonscripterAssets> assets() const { return assets_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<PonscripterAssets> assets_;
  PonscripterVm vm_;
};

} // namespace Ponscripter
