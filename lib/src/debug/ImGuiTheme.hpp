#pragma once

#include <imgui.h>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <memory>

// Shared look of every ImGui context in the engine (debug overlay, launcher).
// Private to the library so public headers never expose ImGui types.
namespace Kotonoha::ImGuiTheme {

class ShaderFilePicker {
  struct Result;
  std::shared_ptr<Result> pending;
  std::string failure;
  static void SDLCALL finished(void *data, const char *const *files, int filter);

public:
  void browse(SDL_Window *window);
  bool poll(UpscaleSettings &settings);
  bool draw(SDL_Window *window, UpscaleSettings &settings);
  const std::string &errorMessage() const { return failure; }
};

// Loads the UI font (with a CJK fallback merged in) into the current context.
// Override with KOTONOHA_IMGUI_FONT / KOTONOHA_IMGUI_CJK_FONT.
void loadFonts(const std::string &fontPath = {});
// Resets the current style to the Kotonoha theme scaled by `scale`.
void apply(float scale);

ImVec4 accent();
ImVec4 muted();
ImVec4 success();
ImVec4 warning();
ImVec4 error();

void statusText(const char *text, bool isError);
// Highlighted call-to-action button.
bool primaryButton(const char *label, const ImVec2 &size = ImVec2(0, 0));

} // namespace Kotonoha::ImGuiTheme
