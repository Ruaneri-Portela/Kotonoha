#include "DemoStage.hpp"
#include "DemoStory.hpp"

#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <cmath>
#include <stdexcept>

namespace KotonohaExample {

void DemoStage::update() {
  const double delta = clock.seconds();
  clock.reset();
  if (!engine.isStoryPaused())
    animation += delta;
}

void DemoStage::render(Kotonoha::RendererRef renderer, float) {
  if (engine.GetTimeline().hasVideoFrame())
    return;
  auto &manager = Kotonoha::GetVNManagerRuntime(engine);
  const int nodeId = manager.getCurrentNodeId();
  const bool cave = nodeId == StoryNode::Cave || (nodeId >= 13 && nodeId <= 15);
  const bool storm =
      nodeId == StoryNode::Storm || (nodeId >= 40 && nodeId <= 44);
  auto &ui = engine.GetUI();
  ui.panel(renderer, {0, 0, 1280, 720},
           cave    ? SDL_Color{14, 21, 30, 255}
           : storm ? SDL_Color{25, 30, 52, 255}
                   : SDL_Color{25, 39, 68, 255});
  for (int i = 0; i < 50; ++i) {
    const float x = static_cast<float>((i * 197 + 41) % 1280);
    const float y = static_cast<float>((i * 59 + 13) % 330);
    const auto alpha = static_cast<Uint8>(100 + 80 * std::sin(animation + i));
    ui.panel(renderer, {x, y, 2, 2}, {200, 220, 255, alpha});
  }
  const SDL_FColor mountain{0.06f, 0.1f, 0.16f, 1};
  const SDL_Vertex peaks[]{
      {{-100, 550}, mountain, {0, 0}}, {{270, 160}, mountain, {0, 0}},
      {{650, 550}, mountain, {0, 0}},  {{430, 550}, mountain, {0, 0}},
      {{970, 100}, mountain, {0, 0}},  {{1450, 550}, mountain, {0, 0}}};
  if (!SDL_RenderGeometry(&renderer, nullptr, peaks, 6, nullptr, 0))
    throw std::runtime_error(SDL_GetError());
  ui.panel(renderer, {0, 440, 1280, 280}, {13, 23, 31, 255});
  if (storm) {
    SDL_SetRenderDrawColor(&renderer, 150, 170, 210, 150);
    for (int i = 0; i < 75; ++i) {
      const float x = static_cast<float>((i * 113) % 1280);
      const float y =
          static_cast<float>(std::fmod(animation * 360 + i * 31, 720));
      SDL_RenderLine(&renderer, x, y, x - 12, y + 28);
    }
  }
  if (!manager.isFinished() && nodeId >= 0)
    ui.text(renderer, manager.getScriptNode(nodeId).name, 40, 60, 1100,
            {165, 190, 220, 255});
}

} // namespace KotonohaExample
