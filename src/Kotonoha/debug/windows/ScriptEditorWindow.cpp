#include <Kotonoha/debug/DebugInternal.hpp>
#include <algorithm>
#include <imgui_stdlib.h>

namespace Kotonoha::Debug {

bool DrawScriptEditorWindow(::Kotonoha::Kotonoha *game,
                            Kotonoha_Game &context,
                            ::Kotonoha::Gameplay *activePlay) {
  if (!scriptEditorVisible)
    return true;

  const bool activeReadOnly =
      activePlay != nullptr && IsReadOnlyAssetPath(activePlay->scriptPath);
  if (activeReadOnly && editorSourcePath != activePlay->scriptPath) {
    LoadSceneEditorFromDisk(activePlay);
  } else if (!activeReadOnly &&
             (context.config.scene != static_cast<size_t>(editorSceneIndex) ||
              editorSceneIndex < 0)) {
    SelectScene(game, static_cast<int>(context.config.scene));
  }

  bool shouldFinishFrame = false;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(400, 300),
      ImVec2(viewport->WorkSize.x * 0.95f, viewport->WorkSize.y * 0.95f));

  if (ImGui::Begin("Script Editor", &scriptEditorVisible)) {
    ::Kotonoha::Gameplay *selected =
        activeReadOnly
            ? activePlay
            : (selectedSceneIndex >= 0 &&
                       selectedSceneIndex <
                           static_cast<int>(game->gameplays.size())
                   ? game->gameplays[selectedSceneIndex]
                   : nullptr);

    if (selected != nullptr) {
        const bool readOnly = activeReadOnly ||
                              IsReadOnlyAssetPath(selected->scriptPath);
        if (activeReadOnly) {
          ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f),
                             "Active mounted scene");
        } else {
          ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f),
                             "Selected Scene: %d", selectedSceneIndex);
        }
        ImGui::TextWrapped("File: %s", selected->scriptPath.empty()
                                           ? "<no file path>"
                                           : selected->scriptPath.c_str());

        if (temporaryGameplayActive &&
            temporaryGameplayIndex == selectedSceneIndex) {
          ImGui::TextColored(ImVec4(0.95f, 0.50f, 0.95f, 1.0f),
                             "Temporary gameplay active.");
        }
        if (readOnly) {
          ImGui::TextDisabled(
              "Read-only: this scene is stored in a mounted asset archive.");
        }

        if (!readOnly && ImGui::Button("Load From Disk") &&
            LoadSceneEditorFromDisk(selected)) {
          RecreateGameplayAtPath(game, context, selectedSceneIndex,
                                 selected->scriptPath);
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y = std::max(avail.y - 70.0f, 120.0f);

        if (ImGui::InputTextMultiline("##scene_editor", &sceneEditorText,
                                      avail,
                                      readOnly ? ImGuiInputTextFlags_ReadOnly
                                               : ImGuiInputTextFlags_None)) {
          editorDirty = true;
          LoadTextEditor(sceneEditorText);
        }

        if (editorStatusBuf[0] != '\0') {
          ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "%s",
                             editorStatusBuf);
        }

        if (!readOnly) {
          ImGui::TextUnformatted(editorDirty ? "* modified" : "saved");
          ImGui::SameLine();
          if (ImGui::Button("Save and Reload") &&
              SaveAndReloadReplace(game, context, selectedSceneIndex)) {
            shouldFinishFrame = true;
          }

          ImGui::SameLine();
          if (ImGui::Button("Save Temporary") &&
              LoadTemporaryGameplayAt(game, context, selectedSceneIndex)) {
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
