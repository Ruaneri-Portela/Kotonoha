// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/DialoguePresentation.hpp>

#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

int main() {
  N2System::SceneGraph graph;
  const auto mockImageSize = [](const N2System::Entity &sprite,
                                float &w, float &h) {
    if (sprite.imagePath.empty())
      return false;
    w = 920.0f;
    h = 130.0f;
    return true;
  };
  const auto inspect = [&] {
    return N2System::inspectDialoguePresentation(
        graph, 1024.0f, 576.0f, "Hello", "Hello", mockImageSize);
  };

  require(!inspect().nativeFrame && !inspect().nativeText,
          "Empty scene requires the host textbox fallback");
  auto *background =
      graph.createEntity("background", N2System::EntityType::Sprite);
  background->imagePath = "cg/bg/scene.png";
  require(!inspect().nativeFrame,
          "Scene CG must not suppress the textbox fallback");

  auto *panel =
      graph.createEntity("MessageWindow/bg", N2System::EntityType::Sprite);
  panel->imagePath = "system/message.png";
  panel->y = 420.0f;
  require(inspect().nativeFrame,
          "Native NSB textbox sprite must suppress duplicate fallback");
  panel->visible = false;
  require(!inspect().nativeFrame,
          "Hidden native window cannot suppress host fallback");
  panel->visible = true;
  panel->opacity = 0.0f;
  require(!inspect().nativeFrame,
          "Transparent native window cannot suppress host fallback");
  panel->opacity = 1.0f;

  auto *nativeText = graph.createEntity("dialogue/text",
                                       N2System::EntityType::Text);
  nativeText->text = "Hello";
  require(inspect().nativeText,
          "NSB text must suppress duplicate text rasterization");
  nativeText->text = "Unrelated";
  require(!inspect().nativeText,
          "Unrelated native text must not suppress the active dialogue");
  nativeText->text = "Hello";

  graph.destroyEntity("MessageWindow/bg");
  require(!inspect().nativeFrame && inspect().nativeText,
          "Native dialogue text may own rendering without a frame");

  graph.clear();
  auto *shape = graph.createEntity("generic", N2System::EntityType::Color);
  shape->x = 40.0f;
  shape->y = 440.0f;
  shape->width = 950.0f;
  shape->height = 110.0f;
  require(inspect().nativeFrame,
          "Native bottom-of-screen window-shaped colour layer is a panel");
  shape->y = 12.0f;
  require(!inspect().nativeFrame,
          "Upper screen decorations must not hide the textbox");

  return 0;
}
