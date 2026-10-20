#include <Kotonoha/debug/DebugInternal.hpp>
#include <algorithm>
#include <imgui_stdlib.h>

namespace Kotonoha::Debug {

bool DrawScriptEditorWindow(::Kotonoha::Kotonoha *game,
                                      Kotonoha_Game &context) {
  if (!scriptEditorVisible)
    return true;

  if (context.config.scene != static_cast<size_t>(editorSceneIndex) ||
      editorSceneIndex < 0) {
    SelectScene(game, static_cast<int>(context.config.scene));
  }

  bool shouldFinishFrame = false;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(400, 300),
      ImVec2(viewport->WorkSize.x * 0.95f, viewport->WorkSize.y * 0.95f));

  if (ImGui::Begin("Script Editor", &scriptEditorVisible)) {
    if (selectedSceneIndex >= 0 &&
        selectedSceneIndex < static_cast<int>(game->gameplays.size())) {
      ::Kotonoha::Gameplay *selected = game->gameplays[selectedSceneIndex];

      if (selected != nullptr) {
        ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f),
                           "Selected Scene: %d", selectedSceneIndex);
        ImGui::TextWrapped("File: %s", selected->scriptPath.empty()
                                           ? "<no file path>"
                                           : selected->scriptPath.c_str());

        if (temporaryGameplayActive &&
            temporaryGameplayIndex == selectedSceneIndex) {
          ImGui::TextColored(ImVec4(0.95f, 0.50f, 0.95f, 1.0f),
                             "Temporary gameplay active.");
        }

        if (ImGui::Button("Load From Disk") &&
            LoadSceneEditorFromDisk(selected)) {
          RecreateGameplayAtPath(game, context, selectedSceneIndex,
                                          selected->scriptPath);
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y = std::max(avail.y - 70.0f, 120.0f);

        if (ImGui::InputTextMultiline("##scene_editor", &sceneEditorText,
                                      avail)) {
          editorDirty = true;
          LoadTextEditor(sceneEditorText);
        }

        if (editorStatusBuf[0] != '\0') {
          ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "%s",
                             editorStatusBuf);
        }

        ImGui::TextUnformatted(editorDirty ? "* modified" : "saved");
        ImGui::SameLine();

        if (ImGui::Button("Save and Reload") &&
            SaveAndReloadReplace(game, context, selectedSceneIndex)) {
          shouldFinishFrame = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Save Temporary") &&
            LoadTemporaryGameplayAt(game, context,
                                             selectedSceneIndex)) {
          shouldFinishFrame = true;
        }

        if (temporaryGameplayActive &&
            temporaryGameplayIndex == selectedSceneIndex) {
          ImGui::SameLine();
          if (ImGui::Button("Revert to Original") &&
              RestoreOriginalPath(game, context)) {
            shouldFinishFrame = true;
          }
        }
      }
    } else {
      ImGui::TextUnformatted("No scene selected.");
    }
  }
  ImGui::End();
  return !shouldFinishFrame;
}

} // namespace Kotonoha::Debug
