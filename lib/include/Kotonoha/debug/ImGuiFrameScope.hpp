#pragma once

#include <imgui.h>
#include <stdexcept>

namespace Kotonoha {

// Balance every ImGui NewFrame, even if a settings panel, file picker or
// debug widget throws. A forgotten EndFrame poisons the next NewFrame and
// triggers the "Forgot to call Render() or EndFrame()" assertion.
class ImGuiFrameScope {
public:
  ImGuiFrameScope() : context(ImGui::GetCurrentContext()) {
    if (!context)
      throw std::runtime_error("No active ImGui context for new frame");
    ImGui::NewFrame();
  }
  ImGuiFrameScope(const ImGuiFrameScope &) = delete;
  ImGuiFrameScope &operator=(const ImGuiFrameScope &) = delete;

  void render() {
    ImGui::SetCurrentContext(context);
    ImGui::Render();
    finished = true;
  }

  ~ImGuiFrameScope() {
    if (!finished) {
      // Restore the frame's own context before closing it. An exception may
      // have changed the current context in a nested settings operation.
      ImGui::SetCurrentContext(context);
      ImGui::EndFrame();
    }
  }

private:
  ImGuiContext *context;
  bool finished = false;
};

} // namespace Kotonoha
