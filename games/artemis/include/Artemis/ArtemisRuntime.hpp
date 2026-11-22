// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Artemis/ArtemisAssets.hpp>
#include <Artemis/ArtemisVm.hpp>
#include <Kotonoha/core/Runtime.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Artemis {

class ArtemisRuntime : public Kotonoha::Runtime {
public:
  explicit ArtemisRuntime(std::shared_ptr<ArtemisAssets> assets = nullptr);
  ~ArtemisRuntime() override;
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
  bool handleBacklogAction() override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  std::string debugNode() const override;
  std::vector<Kotonoha::DebugNode> debugNodes() const override;
  std::vector<std::string> activeTranslationKeys() const override;
  Kotonoha::Translation::SourceTexts translationSources() const override;
  std::vector<Kotonoha::DebugAudioAsset> debugSceneAudio() override;
  void debugJumpToNode(const std::string &id) override;
  void debugRestartScene() override;
  std::string dialogueText() const override;
  std::string dialogueSpeaker() const override;

  ArtemisVm &vm() { return vm_; }
  const ArtemisVm &vm() const { return vm_; }
  std::shared_ptr<ArtemisAssets> assets() const { return assets_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<ArtemisAssets> assets_;
  ArtemisVm vm_;
};

} // namespace Artemis
