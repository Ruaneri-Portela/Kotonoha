#include <Kotonoha/debug/DebugInternal.hpp>
#include <algorithm>
#include <imgui_stdlib.h>

namespace Kotonoha::Debug {

void DrawPlaylistWindow(::Kotonoha::Kotonoha *game,
                                  Kotonoha_Game &context) {
  if (!playlistVisible)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(320, 280),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  if (ImGui::Begin("Playlist", &playlistVisible)) {
    if (context.config.scene < game->gameplays.size() &&
        game->gameplays[context.config.scene] != nullptr) {
      ImGui::TextWrapped(
          "Current File: %s",
          game->gameplays[context.config.scene]->scriptPath.empty()
              ? "<no file path>"
              : game->gameplays[context.config.scene]->scriptPath.c_str());
    } else {
      ImGui::TextUnformatted("Current File: <none>");
    }

    ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "Total Scenes: %d",
                       static_cast<int>(game->gameplays.size()));

    if (ImGui::CollapsingHeader("Append Scenes",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::SetNextItemWidth(-80);
      ImGui::InputTextWithHint("##append_scene_path", "append file or folder",
                               appendBuf, IM_ARRAYSIZE(appendBuf));
      ImGui::SameLine();
      if (ImGui::Button("Append##playlist", ImVec2(70, 0))) {
        errorPopupVisible = !OpenPaths(game, appendBuf, true);
      }
    }

    ImVec2 listSize = ImGui::GetContentRegionAvail();
    listSize.y = std::max(listSize.y - 10.0f, 120.0f);

    if (ImGui::CollapsingHeader("Scene Queue",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::BeginListBox("##scene_playlist", listSize)) {
        for (int i = 0; i < static_cast<int>(game->gameplays.size()); ++i) {
          ::Kotonoha::Gameplay *item = game->gameplays[i];
          if (item == nullptr)
            continue;

          const std::string label =
              item->scriptPath.empty()
                  ? "Scene " + std::to_string(i)
                  : std::to_string(i) + " - " + item->scriptPath;

          const bool selected =
              (context.config.scene == static_cast<size_t>(i));
          if (ImGui::Selectable(label.c_str(), selected)) {
            GoToScene(game, context, i);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndListBox();
      }
    }
  }
  ImGui::End();
}

} // namespace Kotonoha::Debug
