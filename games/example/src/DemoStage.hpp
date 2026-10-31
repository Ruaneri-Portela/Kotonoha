#pragma once

#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Scene.hpp>

namespace KotonohaExample {

class DemoStage final : public Kotonoha::Scene {
  Kotonoha::Clock clock;
  double animation = 0;

public:
  explicit DemoStage(Kotonoha::Engine &engine) : Scene(engine) {}
  Kotonoha::RenderLayer renderLayer() const override {
    return Kotonoha::RenderLayer::Background;
  }
  void update() override;
  void render(Kotonoha::RendererRef renderer, float scale) override;
};

} // namespace KotonohaExample
