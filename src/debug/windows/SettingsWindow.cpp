#include <Kotonoha/debug/DebugInternal.hpp>
#include <Kotonoha/specials/Upscaling.h>
#include <imgui_stdlib.h>

namespace Kotonoha::Debug {

void DrawSettingsFields(Kotonoha_Config *cfg, Kotonoha_Game &context) {
  if (cfg == nullptr)
    return;

  if (ImGui::TreeNodeEx("Display & Graphics", ImGuiTreeNodeFlags_DefaultOpen)) {

    /* ---------------- Seleção de Display/Monitor ---------------- */
    int displayCount = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&displayCount);

    if (displays != nullptr && displayCount > 0) {
      SDL_DisplayID currentDisplay = SDL_GetDisplayForWindow(context.window);
      int currentDisplayIdx = 0;

      // Descobre o índice do display atual
      for (int i = 0; i < displayCount; ++i) {
        if (displays[i] == currentDisplay) {
          currentDisplayIdx = i;
          break;
        }
      }

      // Constrói a lista de nomes dos monitores
      std::vector<std::string> displayNames;
      std::vector<const char *> displayLabels;
      for (int i = 0; i < displayCount; ++i) {
        const char *name = SDL_GetDisplayName(displays[i]);
        std::string label = "Display " + std::to_string(i + 1) +
                            (name ? std::string(" (") + name + ")" : "");
        displayNames.push_back(label);
      }
      for (const auto &name : displayNames) {
        displayLabels.push_back(name.c_str());
      }

      if (ImGui::Combo("Target Display", &currentDisplayIdx,
                       displayLabels.data(),
                       static_cast<int>(displayLabels.size()))) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(displays[currentDisplayIdx], &bounds)) {
          // Checa o estado atual da janela
          const SDL_WindowFlags flags = SDL_GetWindowFlags(context.window);
          const bool isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
          const bool isBorderless =
              ((flags & SDL_WINDOW_BORDERLESS) != 0) && !isFullscreen;

          // 1. Sai de qualquer modo fullscreen/borderless para liberar a janela
          if (isFullscreen) {
            SDL_SetWindowFullscreen(context.window, false);
            SDL_SyncWindow(context.window);
          }
          if (isBorderless) {
            SDL_SetWindowBordered(context.window, false);
            SDL_SyncWindow(context.window);
          }

          // 2. Reposiciona a janela no novo monitor
          SDL_SetWindowPosition(context.window, bounds.x + 50, bounds.y + 50);
          SDL_SyncWindow(context.window);

          // 3. Reorganiza e reaplica o modo de exibição no novo monitor
          if (isFullscreen) {
            SDL_SetWindowFullscreen(context.window, true);
          } else if (isBorderless) {
            SDL_SetWindowBordered(context.window, false);
            SDL_SetWindowPosition(context.window, bounds.x, bounds.y);
            SDL_SetWindowSize(context.window, bounds.w, bounds.h);
          }

          cfg->dirtyConfig = true;
        }
      }
      SDL_free(displays);
    }

    /* ---------------- Modo de Tela (Windowed / Exclusive / Borderless)
     * ---------------- */
    const char *fullscreenModes[] = {"Windowed", "Fullscreen Exclusive",
                                     "Borderless Fullscreen"};

    int currentModeIdx = 0;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(context.window);
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0) {
      currentModeIdx = 1;
    } else if ((flags & SDL_WINDOW_BORDERLESS) != 0) {
      currentModeIdx = 2;
    }

    if (ImGui::Combo("Screen Mode", &currentModeIdx, fullscreenModes,
                     IM_ARRAYSIZE(fullscreenModes))) {
      cfg->dirtyConfig = true;
      switch (currentModeIdx) {
      case 0: // Windowed
        cfg->fullScreen = false;
        SDL_SetWindowFullscreen(context.window, false);
        SDL_SetWindowBordered(context.window, true);
        SDL_SetWindowSize(context.window, cfg->windowSize[0],
                          cfg->windowSize[1]);
        break;

      case 1: // Fullscreen Exclusive
        cfg->fullScreen = true;
        SDL_SetWindowBordered(context.window, true);
        SDL_SetWindowFullscreen(context.window, true);
        break;

      case 2: // Borderless Fullscreen (Sem bordas)
        cfg->fullScreen = false;
        SDL_SetWindowFullscreen(context.window, false);
        SDL_SetWindowBordered(context.window, false);

        SDL_DisplayID currentDisplay = SDL_GetDisplayForWindow(context.window);
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(currentDisplay, &bounds)) {
          SDL_SetWindowPosition(context.window, bounds.x, bounds.y);
          SDL_SetWindowSize(context.window, bounds.w, bounds.h);
        }
        break;
      }
    }

    /* ---------------- VSync ---------------- */
    bool vsyncBool = (cfg->enableVsync != 0);
    if (ImGui::Checkbox("Enable VSync", &vsyncBool)) {
      cfg->enableVsync = vsyncBool ? 1 : 0;
      cfg->dirtyConfig = true;
      SDL_SetRenderVSync(context.render, cfg->enableVsync);
    }

    /* ---------------- Resolução ---------------- */
    ImGui::Text("Window Resolution:");
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("W##res_w", &cfg->windowSize[0]))
      cfg->dirtyConfig = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("H##res_h", &cfg->windowSize[1]))
      cfg->dirtyConfig = true;

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
      SDL_SetWindowSize(context.window, cfg->windowSize[0], cfg->windowSize[1]);
    }

    if (ImGui::SliderFloat("Scaler Factor", &cfg->scalerFactor, 0.5f, 4.0f,
                           "%.2fx")) {
      cfg->dirtyConfig = true;
    }
    if (ImGui::Checkbox("Enable Upscaling", &cfg->upscalingEnabled)) {
      KotonohaUpscalingSetEnabled(cfg->upscalingEnabled);
      cfg->dirtyConfig = true;
    }
    static std::string anime4kBuf = cfg->anime4kFiles ? cfg->anime4kFiles : "";
    if (ImGui::InputText("Anime4K Files (:)", &anime4kBuf)) {
      if (cfg->anime4kFiles)
        SDL_free(cfg->anime4kFiles);
      cfg->anime4kFiles = SDL_strdup(anime4kBuf.c_str());
      KotonohaAnime4KSetFiles(cfg->anime4kFiles);
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Performance & Overlay",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Checkbox("Show FPS Counter", &cfg->showFps))
      cfg->dirtyConfig = true;
    if (ImGui::Checkbox("Show Timestamps", &cfg->showTimestamp))
      cfg->dirtyConfig = true;
    if (ImGui::SliderFloat("Video Cache (s)", &cfg->videoCacheSeconds, 1.0f,
                           15.0f, "%.1f sec")) {
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Debug UI", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::SliderFloat("UI Scale", &cfg->debugUiScale, 0.5f, 3.0f,
                           "%.2fx")) {
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Paths & Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
    static std::string assetsBuf = cfg->assetsPath ? cfg->assetsPath : "";
    if (ImGui::InputText("Assets Path", &assetsBuf)) {
      if (cfg->assetsPath)
        SDL_free(cfg->assetsPath);
      cfg->assetsPath = SDL_strdup(assetsBuf.c_str());
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  ImGui::Separator();
  if (ImGui::Button("Save Configuration", ImVec2(-1, 28))) {
    KotonohaConfigSave(cfg);
    cfg->dirtyConfig = false;
  }

  if (cfg->dirtyConfig) {
    ImGui::TextColored(ImVec4(0.95f, 0.50f, 0.95f, 1.0f), "* Unsaved Settings");
  }
}

void DrawSettingsWindow(Kotonoha_Game &context) {
  if (!settingsVisible)
    return;

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(340, 320),
      ImVec2(viewport->WorkSize.x * 0.9f, viewport->WorkSize.y * 0.9f));

  if (ImGui::Begin("Engine Settings", &settingsVisible,
                   ImGuiWindowFlags_AlwaysAutoResize)) {
    DrawSettingsFields(&context.config, context);
  }
  ImGui::End();
}

} // namespace Kotonoha::Debug
