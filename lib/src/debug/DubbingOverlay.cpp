#include <Kotonoha/debug/DubbingOverlay.hpp>

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/VoiceDubbingApplet.hpp>

#include "ImGuiTheme.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <stdexcept>

namespace Kotonoha {
DubbingOverlay::DubbingOverlay(Engine &engine) : engine_(engine) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGuiTheme::loadFonts();
  ImGuiTheme::apply(1);
  if (!ImGui_ImplSDL3_InitForSDLRenderer(&engine_.GetWindow(),
                                        &engine_.GetRenderer())) {
    ImGui::DestroyContext();
    throw std::runtime_error("Cannot initialize dubbing input backend");
  }
  if (!ImGui_ImplSDLRenderer3_Init(&engine_.GetRenderer())) {
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    throw std::runtime_error("Cannot initialize dubbing renderer");
  }
  applet_ = std::make_unique<VoiceDubbingApplet>(engine_);
}

DubbingOverlay::~DubbingOverlay() {
  applet_->suspend();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

bool DubbingOverlay::handleEvent(EventRef event) {
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_F10) {
      visible_ = !visible_;
      if (!visible_) applet_->suspend();
      return true;
    }
    if (visible_ && event.key.key == SDLK_ESCAPE) {
      visible_ = false;
      applet_->suspend();
      return true;
    }
  }
  if (!visible_) return false;
  ImGui_ImplSDL3_ProcessEvent(&event);
  const auto &io = ImGui::GetIO();
  return io.WantCaptureKeyboard || io.WantCaptureMouse;
}

void DubbingOverlay::render(RendererRef renderer) {
  if (!visible_) return;
  if (!SDL_SetRenderLogicalPresentation(&renderer, 0, 0,
                                        SDL_LOGICAL_PRESENTATION_DISABLED))
    throw std::runtime_error(SDL_GetError());
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  auto &io = ImGui::GetIO();
  int pixelWidth = 0, pixelHeight = 0;
  if (!SDL_GetCurrentRenderOutputSize(&renderer, &pixelWidth, &pixelHeight))
    throw std::runtime_error(SDL_GetError());
  if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0 ||
      pixelWidth <= 0 || pixelHeight <= 0)
    return;
  io.DisplayFramebufferScale = {pixelWidth / io.DisplaySize.x,
                                pixelHeight / io.DisplaySize.y};
  const float displayScale = SDL_GetWindowDisplayScale(&engine_.GetWindow());
  const float pixelDensity = SDL_GetWindowPixelDensity(&engine_.GetWindow());
  if (displayScale <= 0 || pixelDensity <= 0)
    throw std::runtime_error(SDL_GetError());
  const float scale =
      displayScale / pixelDensity * engine_.GetSettings().debugUIScale;
  if (contentScale_ != scale) {
    ImGuiTheme::apply(scale);
    contentScale_ = scale;
  }
  ImGui::NewFrame();
  ImGui::SetNextWindowSize(
      ImVec2(std::min(820 * scale, io.DisplaySize.x),
             std::min(740 * scale, io.DisplaySize.y)),
      ImGuiCond_FirstUseEver);
  const bool wasOpen = visible_;
  if (ImGui::Begin("Dubbing [F10]", &visible_))
    applet_->draw();
  ImGui::End();
  if (wasOpen && !visible_) applet_->suspend();
  ImGui::Render();
  if (!SDL_SetRenderViewport(&renderer, nullptr) ||
      !SDL_SetRenderClipRect(&renderer, nullptr) ||
      !SDL_SetRenderScale(&renderer, io.DisplayFramebufferScale.x,
                          io.DisplayFramebufferScale.y))
    throw std::runtime_error(SDL_GetError());
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), &renderer);
}
} // namespace Kotonoha
