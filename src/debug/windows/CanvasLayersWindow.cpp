#include <Kotonoha/debug/DebugInternal.hpp>

#include <algorithm>

namespace Kotonoha::Debug {
namespace {

const char *StatusName(Kotonoha_Scene_Status status) {
  switch (status) {
  case KOTONOHA_SCENE_DRAW:
    return "Draw";
  case KOTONOHA_SCENE_DRAW_LAST:
    return "Draw (last)";
  case KOTONOHA_SCENE_DRAW_OVERLAYED:
    return "Overlay";
  case KOTONOHA_SCENE_WAITING:
    return "Waiting";
  case KOTONOHA_SCENE_COMPLETE:
    return "Complete";
  case KOTONOHA_SCENE_NULL:
    return "Idle";
  default:
    return "Error";
  }
}

} // namespace

void DrawCanvasLayersWindow(::Kotonoha::Gameplay *play) {
  if (!canvasLayersVisible) {
    return;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(
      ImVec2(viewport->WorkSize.x * 0.45f, viewport->WorkSize.y * 0.70f),
      ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Canvas Layers", &canvasLayersVisible)) {
    if (play == nullptr || play->drawCanvas == nullptr) {
      ImGui::TextDisabled("No active scene.");
    } else {
      const std::vector<::Kotonoha::CanvasLayerInfo> layers =
          play->drawCanvas->GetLayerInfo();
      ImGui::Text("%d layers (drawn bottom to top)",
                  static_cast<int>(layers.size()));
      ImGui::Separator();

      for (size_t i = 0; i < layers.size(); ++i) {
        const ::Kotonoha::CanvasLayerInfo &layer = layers[i];
        ImGui::PushID(static_cast<int>(i));

        char header[96];
        SDL_snprintf(header, sizeof(header), "z=%d  %s  [%s]%s", layer.zIndex,
                     layer.name, StatusName(layer.status),
                     layer.retained ? " (retained)" : "");
        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
          float width = 0.0f;
          float height = 0.0f;
          if (layer.texture == nullptr ||
              !SDL_GetTextureSize(layer.texture, &width, &height) ||
              width <= 0.0f || height <= 0.0f) {
            ImGui::TextDisabled("Layer has no texture yet.");
          } else {
            const float available =
                std::max(ImGui::GetContentRegionAvail().x, 1.0f);
            const float scale = std::min(1.0f, available / width);
            // Checkerboard-free dark backdrop so transparent layers are
            // visible.
            const ImVec2 size(width * scale, height * scale);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                origin, ImVec2(origin.x + size.x, origin.y + size.y),
                IM_COL32(40, 40, 40, 255));
            ImGui::Image(layer.texture, size);
            ImGui::Text("%.0f x %.0f", width, height);
          }
        }
        ImGui::PopID();
      }
    }
  }
  ImGui::End();
}

} // namespace Kotonoha::Debug
