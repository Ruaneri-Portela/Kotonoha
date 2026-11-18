#include <Kotonoha/debug/ImGuiFrameScope.hpp>

#include <imgui.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool success, const char *why) {
  if (!success)
    throw std::runtime_error(why);
}
void test() {
  IMGUI_CHECKVERSION();
  ImGuiContext *context = ImGui::CreateContext();
  check(context, "Create the isolated ImGui context");
  ImGui::SetCurrentContext(context);
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640, 480);
  io.DeltaTime = 1.0f / 60.0f;
  // No window or SDL backend is needed to exercise frame ownership.
  unsigned char *fontPixels = nullptr;
  int width = 0, height = 0;
  io.Fonts->GetTexDataAsRGBA32(&fontPixels, &width, &height);
  check(fontPixels && width > 0 && height > 0,
        "Build a font atlas before the isolated frame");

  // A UI callback throwing before Render() must still close the frame.
  bool caught = false;
  try {
    Kotonoha::ImGuiFrameScope frame;
    ImGui::Begin("Interrupted settings");
    ImGui::TextUnformatted("No renderer is needed");
    ImGui::End();
    throw std::runtime_error("Synthetic widget exception");
  } catch (const std::runtime_error &) {
    caught = true;
  }
  check(caught, "The synthetic early exit must throw");
  // This NewFrame used to assert if the preceding frame was not EndFrame'd.
  {
    Kotonoha::ImGuiFrameScope frame;
    ImGui::Begin("Recovered frame");
    ImGui::TextUnformatted("Available after exception");
    ImGui::End();
    frame.render();
  }
  check(ImGui::GetFrameCount() == 2,
        "The second frame must start after the early-exit frame");

  // Also cover normal early return without Render, such as minimized windows.
  {
    Kotonoha::ImGuiFrameScope frame;
  }
  {
    Kotonoha::ImGuiFrameScope frame;
    ImGui::Begin("Recovered minimized frame");
    ImGui::End();
    frame.render();
  }
  ImGui::DestroyContext(context);
}
} // namespace

int main() {
  try {
    test();
    std::cout << "Kotonoha.ImGuiFrameScope PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Kotonoha.ImGuiFrameScope FAIL: " << error.what() << '\n';
    return 1;
  }
}
