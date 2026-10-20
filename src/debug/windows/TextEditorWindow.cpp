#include <Kotonoha/debug/DebugInternal.hpp>
#include <imgui_stdlib.h>

namespace Kotonoha::Debug {

static void DrawTextEventList() {
  ImVec2 listSize = ImGui::GetContentRegionAvail();
  listSize.y -= 50.0f;

  if (ImGui::BeginChild("##text_event_list", listSize,
                        ImGuiChildFlags_Borders)) {
    if (textEditorEntries.empty()) {
      ImGui::TextDisabled("No PRINT_TEXT or SetSELECT in this scene.");
    }

    bool changed = false;

    for (size_t i = 0; i < textEditorEntries.size(); ++i) {
      TextEditorEntry &entry = textEditorEntries[i];
      ImGui::PushID(static_cast<int>(i));

      ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "%s  %s - %s",
                         entry.isSelection ? "Choice" : "Speak",
                         entry.startTime.c_str(), entry.endTime.c_str());

      if (entry.isSelection) {
        for (size_t option = 0; option < entry.options.size(); ++option) {
          ImGui::SetNextItemWidth(-10);
          const std::string label = "Option " + std::to_string(option + 1);
          if (ImGui::InputText(label.c_str(), &entry.options[option])) {
            changed = true;
          }
        }
      } else {
        ImGui::SetNextItemWidth(-10);
        if (ImGui::InputText("Character", &entry.character))
          changed = true;

        ImGui::SetNextItemWidth(-10);
        if (ImGui::InputText("Dialogue", &entry.text))
          changed = true;
      }

      ImGui::Separator();
      ImGui::PopID();
    }

    if (changed) {
      SyncTextEditorToScript();
      editorDirty = true;
    }
  }
  ImGui::EndChild();
}

bool DrawTextEditorWindow(::Kotonoha::Kotonoha *game,
                                    Kotonoha_Game &context) {
  if (!textEditorVisible)
    return true;

  if (context.config.scene != static_cast<size_t>(editorSceneIndex) ||
      editorSceneIndex < 0) {
    SelectScene(game, static_cast<int>(context.config.scene));
  } else if (sceneEditorText != textEditorSyncedScript) {
    LoadTextEditor(sceneEditorText);
  }

  bool shouldFinishFrame = false;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(380, 300),
      ImVec2(viewport->WorkSize.x * 0.95f, viewport->WorkSize.y * 0.95f));

  if (ImGui::Begin("Text Editor", &textEditorVisible)) {
    if (selectedSceneIndex >= 0 &&
        selectedSceneIndex < static_cast<int>(game->gameplays.size()) &&
        game->gameplays[selectedSceneIndex] != nullptr) {
      ::Kotonoha::Gameplay *selected = game->gameplays[selectedSceneIndex];

      ImGui::TextColored(ImVec4(0.80f, 0.60f, 0.95f, 1.0f), "Scene %d",
                         selectedSceneIndex + 1);
      ImGui::TextWrapped("File: %s", selected->scriptPath.empty()
                                         ? "<No path to file>"
                                         : selected->scriptPath.c_str());
      ImGui::Separator();

      DrawTextEventList();

      if (ImGui::Button("Save and Reload")) {
        if (SaveAndReloadReplace(game, context, selectedSceneIndex)) {
          shouldFinishFrame = true;
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Save Temporary")) {
        if (LoadTemporaryGameplayAt(game, context,
                                             selectedSceneIndex)) {
          shouldFinishFrame = true;
        }
      }

      if (temporaryGameplayActive &&
          temporaryGameplayIndex == selectedSceneIndex) {
        ImGui::SameLine();
        if (ImGui::Button("Revert to Original")) {
          if (RestoreOriginalPath(game, context)) {
            shouldFinishFrame = true;
          }
        }
      }
    }
  }
  ImGui::End();
  return !shouldFinishFrame;
}

} // namespace Kotonoha::Debug
