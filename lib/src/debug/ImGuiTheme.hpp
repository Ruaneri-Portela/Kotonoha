#pragma once

#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <memory>
#include <string>

namespace Kotonoha::ImGuiTheme {

void loadFonts(const std::string &fontPath = {});
void apply(float scale = 1.0f);
ImVec4 accent();
ImVec4 success();
ImVec4 warning();
ImVec4 error();
ImVec4 muted();
void statusText(const char *text, bool error);
bool primaryButton(const char *label, ImVec2 size = {});

class ShaderFilePicker {
  struct State;
  std::shared_ptr<State> state;
  std::string error;
  static void SDLCALL finished(void *userdata, const char *const *files, int filter);

public:
  ShaderFilePicker() = default;
  ~ShaderFilePicker() = default;
  void browse(SDL_Window *window);
  void poll(UpscaleSettings &settings);
  bool draw(SDL_Window *window, UpscaleSettings &settings);
  const std::string &errorMessage() const { return error; }
};

} // namespace Kotonoha::ImGuiTheme
