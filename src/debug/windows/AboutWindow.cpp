#include <Kotonoha/debug/DebugInternal.hpp>

namespace Kotonoha::Debug {

void DrawAboutFields() {
  ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.98f, 1.0f),
                     "Kotonoha Visual Novel Engine");
  ImGui::Separator();
  ImGui::TextWrapped(
      "Kotonoha is a Visual Novel engine built upon SDL (Simple DirectMedia "
      "Layer) "
      "and FFmpeg. It is a reimagining and reimplementation of the FORIS "
      "engine, "
      "originally used in the \"Days\" series games produced by 0verflow.\n\n"
      "Its goal is to provide a versatile and robust platform for creating and "
      "playing visual novels.");

  ImGui::Spacing();
  if (ImGui::TreeNodeEx("Project Links", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BulletText("GitHub: https://github.com/Ruaneri-Portela/Kotonoha");
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Contributors", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BulletText("Ruaneri Ferreira Portela");
    ImGui::BulletText("tsubaki-0xSUZU");
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("License", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("MIT License");
    ImGui::TextDisabled("Copyright (c) 2023 Ruaneri Ferreira Portela");
    ImGui::TreePop();
  }
}

void DrawAboutWindow() {
  if (!aboutVisible)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 40, viewport->WorkPos.y + 40),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(380, 320),
      ImVec2(viewport->WorkSize.x * 0.8f, viewport->WorkSize.y * 0.8f));

  if (ImGui::Begin("About Kotonoha", &aboutVisible,
                   ImGuiWindowFlags_AlwaysAutoResize)) {
    DrawAboutFields();
  }
  ImGui::End();
}

} // namespace Kotonoha::Debug
