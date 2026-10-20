#include <Kotonoha/debug/DebugInternal.hpp>

namespace Kotonoha::Debug {

void DrawAboutFields() {
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(KotonohaAboutText());
  ImGui::PopTextWrapPos();
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
