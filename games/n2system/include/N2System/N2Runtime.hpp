// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/core/Runtime.hpp>
#include <N2System/N2ArchiveAssets.hpp>
#include <N2System/ScriptVm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace N2System {

class N2Runtime : public Kotonoha::Runtime {
public:
  explicit N2Runtime(std::shared_ptr<N2ArchiveAssets> assets = nullptr);
  ~N2Runtime() override;

  void init(Kotonoha::Engine &engine) override;
  void initGlobal(Kotonoha::Engine &engine) override;
  void initSplash(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;

  void update() override;
  void renderGameplay(Kotonoha::RendererRef renderer, float scale) override;
  bool onEvent(Kotonoha::EventRef event) override;

  bool handleAdvanceAction() override;

  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;

  std::string dialogueText() const override;
  std::string dialogueSpeaker() const override;

  ScriptVm &vm() { return vm_; }
  const ScriptVm &vm() const { return vm_; }

  std::shared_ptr<N2ArchiveAssets> assets() const { return assets_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<N2ArchiveAssets> assets_;
  ScriptVm vm_;
};

} // namespace N2System
