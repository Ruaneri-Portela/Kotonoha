#pragma once

#include "DemoStory.hpp"
#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <memory>
#include <optional>

namespace Kotonoha {
class VNSaveMenu;
}

namespace KotonohaExample {

class DemoStage;

class DemoGame final : public Kotonoha::Runtime {
  Kotonoha::Engine *engine = nullptr;
  DemoState state;
  std::unique_ptr<DemoStage> stage;
  std::unique_ptr<Kotonoha::VNSaveMenu> saveMenu;
  std::optional<Kotonoha::VNSave> pendingLoad;
  DemoState restoredState;

  std::string savePath() const;
  void openSaves(bool load);
  void saveLoadAction(bool load);
  void requestPlayerName();

public:
  DemoGame();
  ~DemoGame() override;
  void init(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;
  void onEvent(Kotonoha::EventRef event) override;
  void update() override;
  void saveGame(const std::string &path) override;
  void loadGame(const std::string &path) override;
};

} // namespace KotonohaExample
