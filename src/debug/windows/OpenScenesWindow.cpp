#include <Kotonoha/debug/DebugInternal.hpp>
#include <imgui_stdlib.h>

namespace Kotonoha::Debug {

void DrawOpenScenesWindow(::Kotonoha::Kotonoha *game, Kotonoha_Game &context,
                          bool &shouldExit) {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(360, 280),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  if (ImGui::Begin("Open Scenes")) {
    if (ImGui::CollapsingHeader("Load Scenes",
                                ImGuiTreeNodeFlags_DefaultOpen)) {

      ImGui::SetNextItemWidth(-80);
      const bool enterPressed = ImGui::InputTextWithHint(
          "##open_script_path", "file1.ks;file2.ks;folder", openBuf,
          IM_ARRAYSIZE(openBuf), ImGuiInputTextFlags_EnterReturnsTrue);

      ImGui::SameLine();
      if (enterPressed || ImGui::Button("Open", ImVec2(70, 0))) {
        errorPopupVisible = !OpenPaths(game, openBuf, false);
        selectedSceneIndex = -1;
        editorSceneIndex = -1;
        sceneEditorText.clear();
        editorDirty = false;

        temporaryGameplayActive = false;
        temporaryGameplayIndex = -1;

        if (!game->gameplays.empty()) {
          context.config.scene = 0;
          TryAutoLoadCurrentScene(game, context);
        }
      }

      if (ImGui::Button("Append Scenes", ImVec2(-1, 0))) {
        errorPopupVisible = !OpenPaths(game, openBuf, true);
        if (selectedSceneIndex < 0 && !game->gameplays.empty()) {
          TryAutoLoadCurrentScene(game, context);
        }
      }

      if (errorPopupVisible) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.45f, 1.0f),
                           "Failed to open one or more paths.");
      }

      if (!game->gameplays.empty()) {
        ImGui::Separator();
        ImGui::Text("Loaded scenes: %d",
                    static_cast<int>(game->gameplays.size()));

        if (ImGui::Button("Start from First", ImVec2(-1, 0))) {
          context.config.scene = 0;
          TryAutoLoadCurrentScene(game, context);
        }

        ImGui::Checkbox("Playlist", &playlistVisible);
        ImGui::SameLine();
        ImGui::Checkbox("Script Editor", &scriptEditorVisible);
        ImGui::SameLine();
        ImGui::Checkbox("Text Editor", &textEditorVisible);
        ImGui::SameLine();
        ImGui::Checkbox("Settings Window", &settingsVisible);
        ImGui::SameLine();
        ImGui::Checkbox("About", &aboutVisible);
      }

      ImGui::Checkbox("Log", &logVisible);

      DrawAudio(context);

      if (ImGui::CollapsingHeader("Engine Settings")) {
        DrawSettingsFields(&context.config, context);
      }

      if (ImGui::CollapsingHeader("About Kotonoha")) {
        DrawAboutFields();
      }

      ImGui::Separator();
      if (ImGui::Button("Exit Application", ImVec2(-1, 0))) {
        shouldExit = true;
      }
    }
  }
  ImGui::End();
}

} // namespace Kotonoha::Debug
