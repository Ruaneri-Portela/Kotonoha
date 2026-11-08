#pragma once

#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <functional>
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
// Draws a section header with a small accent mark.
void sectionHeader(const char *label);

// --- Modern UI helpers ---

// Pushes a rounded card background (call ImGui::EndChild() or ImGui::PopStyleVar
// after content). Returns true when the card is open/collapsible and hovered.
void cardBegin(const char *id, bool collapsible = false, bool *open = nullptr);
void cardEnd();

// A small colored badge drawn inline (e.g. status indicators).
void badge(const char *label, ImVec4 color);

// A label + value row with right-aligned value.
void valueRow(const char *label, const char *value, ImVec4 valueColor = {});

// A modern toggle switch (checkbox replacement).
bool toggleSwitch(const char *label, bool *v);

// A button with a small leading emoji/icon text.
bool iconButton(const char *icon, const char *label, ImVec2 size = {});

// Draws a Kotonoha-branded header band (accent strip + title + subtitle).
void kotonohaHeader(const char *title, const char *subtitle, float scale);

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
