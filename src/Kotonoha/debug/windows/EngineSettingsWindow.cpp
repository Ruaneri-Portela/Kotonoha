#include <Kotonoha/debug/DebugInternal.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/specials/Upscaling.h>
#include <Kotonoha/utils/AtomicUi.hpp>
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
        DisplayMode mode;
        if (GetDisplayMode(context, &mode) &&
            SetDisplay(context, mode, displays[currentDisplayIdx])) {
          cfg->dirtyConfig = true;
        } else if (SDL_GetError()[0] != '\0') {
          SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                       "Could not move Kotonoha to the selected display: %s",
                       SDL_GetError());
        }
      }
      SDL_free(displays);
    }

    /* ---------------- Modo de Tela (Windowed / Exclusive / Borderless)
     * ---------------- */
    const char *fullscreenModes[] = {"Windowed", "Fullscreen Exclusive",
                                     "Borderless Fullscreen"};

    DisplayMode displayMode = DisplayMode::Windowed;
    if (GetDisplayMode(context, &displayMode)) {
      int currentModeIdx = static_cast<int>(displayMode);
      if (ImGui::Combo("Screen Mode", &currentModeIdx, fullscreenModes,
                       IM_ARRAYSIZE(fullscreenModes)) &&
          currentModeIdx >= static_cast<int>(DisplayMode::Windowed) &&
          currentModeIdx <=
              static_cast<int>(DisplayMode::BorderlessFullscreen)) {
        const auto selectedMode = static_cast<DisplayMode>(currentModeIdx);
        if (SetDisplay(context, selectedMode)) {
          cfg->dirtyConfig = true;
        }
      }
    } else {
      SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                   "Could not query Kotonoha display mode: %s", SDL_GetError());
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
    if (EditAtomic(cfg->windowSize[0],
                   [](int *v) { return ImGui::InputInt("W##res_w", v); }))
      cfg->dirtyConfig = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (EditAtomic(cfg->windowSize[1],
                   [](int *v) { return ImGui::InputInt("H##res_h", v); }))
      cfg->dirtyConfig = true;

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
      SDL_SetWindowSize(context.window, cfg->windowSize[0], cfg->windowSize[1]);
    }

    if (EditAtomic(cfg->scalerFactor, [](float *v) {
          return ImGui::SliderFloat("Scaler Factor", v, 0.5f, 4.0f, "%.2fx");
        })) {
      KotonohaUpscalingSetFactor(cfg->scalerFactor);
      cfg->dirtyConfig = true;
    }
    if (EditAtomic(cfg->upscalingEnabled, [](bool *v) {
          return ImGui::Checkbox("Enable Upscaling", v);
        })) {
      KotonohaUpscalingSetEnabled(cfg->upscalingEnabled);
      cfg->dirtyConfig = true;
    }
    int preset = std::clamp(cfg->upscalerPreset.load(), 0, 3);
    const char *presetNames[] = {"Legacy Anime4K B-S", "Anime Light (ArtCNN C4F16)",
                                 "Anime Clean (ArtCNN C4F16 DS)",
                                 "Anime Quality (ArtCNN C4F32)"};
    if (ImGui::Combo("Upscaler Preset", &preset, presetNames,
                     IM_ARRAYSIZE(presetNames))) {
      cfg->upscalerPreset = preset;
      KotonohaUpscalingSetPreset(static_cast<Kotonoha_UpscalerPreset>(preset));
      cfg->dirtyConfig = true;
    }
    const float activeFactor = KotonohaUpscalingGetFactor();
    if (!KotonohaUpscalingIsEnabled()) {
      ImGui::TextDisabled("Upscaling is disabled in the active configuration.");
    } else if (activeFactor <= 1.0f) {
      ImGui::TextDisabled("Set the scaler factor above 1.0x to upscale.");
    } else if (Kotonoha_GetUpscalerType() == KOTONOHA_UPSCALER_NONE) {
      ImGui::Text("Active: SDL texture scaling (%.2fx)", activeFactor);
    } else {
      ImGui::Text("Active: %s (%.2fx)", Kotonoha_GetUpscalerName(),
                  activeFactor);
    }
    ImGui::TextDisabled(
        "Applied when image textures are created and when video frames change.");
    static std::string mpvFilesBuf = cfg->mpvUpscalerFiles ? cfg->mpvUpscalerFiles : "";
    if (ImGui::InputText("MPVCompatibleUpscaler Files (:)", &mpvFilesBuf)) {
      if (cfg->mpvUpscalerFiles)
        SDL_free(cfg->mpvUpscalerFiles);
      cfg->mpvUpscalerFiles = SDL_strdup(mpvFilesBuf.c_str());
      KotonohaUpscalingSetCustomShaderFiles(cfg->mpvUpscalerFiles);
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Performance & Overlay",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    if (EditAtomic(cfg->showFps, [](bool *v) {
          return ImGui::Checkbox("Show FPS Counter", v);
        }))
      cfg->dirtyConfig = true;
    if (EditAtomic(cfg->showTimestamp, [](bool *v) {
          return ImGui::Checkbox("Show Timestamps", v);
        }))
      cfg->dirtyConfig = true;
    if (EditAtomic(cfg->mediaCacheSeconds, [](float *v) {
          return ImGui::SliderFloat("Media Cache (s)", v, 1.0f, 15.0f,
                                    "%.1f sec");
        })) {
      cfg->dirtyConfig = true;
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx("Debug UI", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (EditAtomic(cfg->debugUiScale, [](float *v) {
          return ImGui::SliderFloat("UI Scale", v, 0.5f, 3.0f, "%.2fx");
        })) {
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

void DrawEngineSettingsWindow(Kotonoha_Game &context) {
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
