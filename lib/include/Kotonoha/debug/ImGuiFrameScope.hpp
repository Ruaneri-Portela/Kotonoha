#pragma once

#include <imgui.h>
#include <stdexcept>

namespace Kotonoha {

// RAII wrapper: calls ImGui::NewFrame() on construction, ImGui::Render() or
// ImGui::EndFrame() on destruction. The caller MUST guarantee that no
// re-entrant NewFrame() can happen while a scope is alive — use a boolean
// guard in the owning draw loop (e.g. Launcher::drawing_).
class ImGuiFrameScope {
public:
  ImGuiFrameScope() {
    if (!ImGui::GetCurrentContext())
      throw std::runtime_error("No active ImGui context for new frame");
    ImGui::NewFrame();
  }
  ImGuiFrameScope(const ImGuiFrameScope &) = delete;
  ImGuiFrameScope &operator=(const ImGuiFrameScope &) = delete;

  void render() {
    ImGui::Render();
    rendered = true;
  }

  ~ImGuiFrameScope() {
    if (!rendered)
      ImGui::EndFrame();
  }

private:
  bool rendered = false;
};

} // namespace Kotonoha
