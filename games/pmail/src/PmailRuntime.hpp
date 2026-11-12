#pragma once

#include "Pmail/Port.hpp"

#include <Kotonoha/core/Runtime.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

class PmailRuntime : public Kotonoha::Runtime {
public:
  explicit PmailRuntime(std::string gameDirectory);
  ~PmailRuntime() override = default;

  void initGlobal(Kotonoha::Engine &engine) override;
  void initMenu(Kotonoha::Engine &engine) override;
  void initGameplay(Kotonoha::Engine &engine) override;

  SDL_Point viewport() const override { return {800, 600}; }
  bool continuousTouch() const override { return true; }
  void render(Kotonoha::RendererRef renderer, float scale) override;
  void update() override;
  bool onEvent(Kotonoha::EventRef event) override;

  std::string debugNode() const override;

private:
  bool loadScriptPack();

  std::string gameDirectory_;
  Kotonoha::Engine *engine_ = nullptr;
  std::unique_ptr<PmailGame> game_;
  std::string status_;
  bool running_ = false;
};

} // namespace Kotonoha::PmailPort
