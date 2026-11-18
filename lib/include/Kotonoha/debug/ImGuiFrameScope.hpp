#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <stdexcept>

namespace Kotonoha {

// Balance every ImGui NewFrame, even if a settings panel, file picker, macOS
// Cocoa live-resize timer callback, or debug widget re-enters or throws.
// Closing any unclosed frame before NewFrame() guarantees that ImGui's
// ErrorCheckNewFrameSanityChecks() assertion can never fire.
class ImGuiFrameScope {
public:
  ImGuiFrameScope() : context(ImGui::GetCurrentContext()) {
    if (!context)
      throw std::runtime_error("No active ImGui context for new frame");
    if (context->WithinFrameScope ||
        (context->FrameCount > 0 &&
         context->FrameCountEnded != context->FrameCount)) {
      ImGui::EndFrame();
    }
    ImGui::NewFrame();
  }
  ImGuiFrameScope(const ImGuiFrameScope &) = delete;
  ImGuiFrameScope &operator=(const ImGuiFrameScope &) = delete;

  void render() {
    ImGui::SetCurrentContext(context);
    if (context->WithinFrameScope ||
        (context->FrameCount > 0 &&
         context->FrameCountEnded != context->FrameCount)) {
      ImGui::Render();
    }
    finished = true;
  }

  ~ImGuiFrameScope() {
    if (!finished && context) {
      // Restore the frame's own context before closing it. An exception or
      // re-entrant OS modal callback may have changed the current context.
      ImGui::SetCurrentContext(context);
      if (context->WithinFrameScope ||
          (context->FrameCount > 0 &&
           context->FrameCountEnded != context->FrameCount)) {
        ImGui::EndFrame();
      }
    }
  }

private:
  ImGuiContext *context;
  bool finished = false;
};

} // namespace Kotonoha
