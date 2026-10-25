#include <cstring>
#include <Kotonoha/debug/DebugInternal.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/debug/LogPanel.hpp>
#include <Kotonoha/specials/Upscaling.h>
#include <algorithm>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_internal.h>

namespace Kotonoha::Debug {

namespace {

ImGuiStyle debugUiBaseStyle;
bool debugUiBaseStyleReady = false;
float appliedDebugUiScale = 1.0f;
bool sdlBackendInitialized = false;
bool rendererBackendInitialized = false;
Kotonoha_Game *debugUiGameContext = nullptr;

} // namespace

bool IsAnyWindowFocused() { return anyWindowFocused; }

bool IsMouseCaptured() {
  return ImGui::GetCurrentContext() != nullptr &&
         ImGui::GetIO().WantCaptureMouse;
}

bool IsScriptEditorVisible() { return scriptEditorVisible; }

static const char *GetFocusedWindowName() {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  return (ctx && ctx->NavWindow) ? ctx->NavWindow->Name : nullptr;
}

void UpdateFocusState() {
  const char *focused = GetFocusedWindowName();
  anyWindowFocused = focused != nullptr && std::strcmp(focused, "Debug") != 0;
}

static void ApplyPurpleTheme() {
  ImGuiStyle &style = ImGui::GetStyle();

  style.WindowRounding = 8.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 5.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 5.0f;
  style.TabRounding = 5.0f;

  style.WindowPadding = ImVec2(10.0f, 10.0f);
  style.FramePadding = ImVec2(6.0f, 4.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);

  ImVec4 *colors = style.Colors;

  // Fundo e Estrutura Principal
  colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.07f, 0.13f, 0.96f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.05f, 0.10f, 0.80f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.08f, 0.16f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.28f, 0.18f, 0.42f, 0.60f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  // Cabeçalhos e Títulos
  colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.08f, 0.18f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.24f, 0.12f, 0.36f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.05f, 0.12f, 0.75f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.08f, 0.18f, 1.00f);

  // Campos de Entrada (InputText, Checkbox, Slider)
  colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.11f, 0.24f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.15f, 0.36f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.18f, 0.45f, 1.00f);

  // Botões
  colors[ImGuiCol_Button] = ImVec4(0.32f, 0.16f, 0.48f, 0.85f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.42f, 0.22f, 0.62f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.50f, 0.26f, 0.72f, 1.00f);

  // Collapsing Headers & Selectables
  colors[ImGuiCol_Header] = ImVec4(0.28f, 0.15f, 0.42f, 0.75f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.20f, 0.56f, 0.85f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.46f, 0.24f, 0.68f, 1.00f);

  // Elementos de Interação (Sliders, Checkmarks)
  colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.45f, 0.95f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.56f, 0.28f, 0.82f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.68f, 0.34f, 0.98f, 1.00f);

  // Separadores e Scrollbars
  colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.18f, 0.42f, 0.60f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.24f, 0.65f, 0.80f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.60f, 0.30f, 0.85f, 1.00f);

  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.05f, 0.12f, 0.60f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.14f, 0.36f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.20f, 0.52f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.48f, 0.25f, 0.68f, 1.00f);

  // Texto
  colors[ImGuiCol_Text] = ImVec4(0.94f, 0.92f, 0.98f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.44f, 0.58f, 1.00f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.48f, 0.22f, 0.68f, 0.45f);
}

bool Init(Kotonoha_Game &gameContext, std::vector<Uint8> font) {
  if (ImGui::GetCurrentContext() == nullptr || gameContext.window == nullptr ||
      gameContext.render == nullptr) {
    SDL_SetError("Debug UI requires an ImGui context, window, and renderer");
    return false;
  }

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  ImFont *mainFont = nullptr;

  if (!font.empty()) {
    const size_t fontDataSize = font.size();
    void *fontData = SDL_malloc(fontDataSize);
    if (fontData != nullptr) {
      SDL_memcpy(fontData, font.data(), fontDataSize);
      ImFontConfig fontConfig;
      fontConfig.RasterizerDensity =
          std::max(1.0f, SDL_GetWindowPixelDensity(gameContext.window));
      mainFont = io.Fonts->AddFontFromMemoryTTF(
          fontData, static_cast<int>(fontDataSize), 16.0f, &fontConfig);
    }
  }

  if (mainFont == nullptr) {
    mainFont = io.Fonts->AddFontDefault();
  }

  io.FontDefault = mainFont;

  ApplyPurpleTheme();
  debugUiBaseStyle = ImGui::GetStyle();
  debugUiBaseStyleReady = true;
  appliedDebugUiScale = 0.0f;

  sdlBackendInitialized =
      ImGui_ImplSDL3_InitForSDLRenderer(gameContext.window, gameContext.render);
  if (!sdlBackendInitialized) {
    SDL_SetError("Failed to initialize ImGui SDL3 debug backend");
    return false;
  }
  rendererBackendInitialized =
      ImGui_ImplSDLRenderer3_Init(gameContext.render);
  if (!rendererBackendInitialized) {
    SDL_SetError("Failed to initialize ImGui SDL renderer debug backend");
    Shutdown();
    return false;
  }
  InstallLogCapture();
  debugUiGameContext = &gameContext;
  return true;
}

void Shutdown() {
  UninstallLogCapture();
  if (rendererBackendInitialized) {
    ImGui_ImplSDLRenderer3_Shutdown();
    rendererBackendInitialized = false;
  }
  if (sdlBackendInitialized) {
    ImGui_ImplSDL3_Shutdown();
    sdlBackendInitialized = false;
  }
  debugUiBaseStyleReady = false;
  debugUiGameContext = nullptr;
}

bool BeginWorkspaceFrame(Kotonoha_Game &context,
                         WindowMenuExtension extension, void *userdata) {
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  DrawWindowMenu(extension, userdata);
  return true;
}

bool FinishWorkspaceFrame(Kotonoha_Game &context) {
  ImGui::Render();
  const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
  if (!SDL_SetRenderScale(context.render, fbScale.x, fbScale.y)) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to scale debug UI rendering: %s", SDL_GetError());
    return false;
  }
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
  if (!SDL_SetRenderScale(context.render, 1.0f, 1.0f)) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "Failed to restore renderer scale after debug UI: %s",
                 SDL_GetError());
    return false;
  }
  return true;
}

// The master panel is always present (no close/collapse). It replaces the
// macOS-style main menu bar and hosts every debug window toggle.
void DrawWindowMenu(WindowMenuExtension extension, void *userdata) {
  // NoNav: keys pass through to the game, so they must not also drive the
  // checkboxes here.
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNav;
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Debug", nullptr, flags)) {
    ImGui::Checkbox("Scene Loader", &sceneLoaderVisible);
    ImGui::Checkbox("Playback & Mixer", &playbackVisible);
    ImGui::Checkbox("Playlist", &playlistVisible);
    ImGui::Checkbox("Script Editor", &scriptEditorVisible);
    ImGui::Checkbox("Text Editor", &textEditorVisible);
    ImGui::Checkbox("Scene Buffers", &sceneBuffersVisible);
    ImGui::Checkbox("Canvas Layers", &canvasLayersVisible);
    ImGui::Checkbox("Engine Settings", &settingsVisible);
    if (debugUiGameContext != nullptr) {
      auto &config = debugUiGameContext->config;
      bool upscalingEnabled = config.upscalingEnabled.load();
      if (ImGui::Checkbox("Enable Upscaling", &upscalingEnabled)) {
        config.upscalingEnabled.store(upscalingEnabled);
        KotonohaUpscalingSetEnabled(upscalingEnabled);
        config.dirtyConfig = true;
      }

      int preset = std::clamp(config.upscalerPreset.load(), 0, 3);
      const char *presetNames[] = {"Legacy Anime4K B-S", "Anime Light (ArtCNN C4F16)",
                                   "Anime Clean (ArtCNN C4F16 DS)",
                                   "Anime Quality (ArtCNN C4F32)"};
      if (ImGui::Combo("Upscaler Preset", &preset, presetNames,
                       IM_ARRAYSIZE(presetNames))) {
        config.upscalerPreset.store(preset);
        KotonohaUpscalingSetPreset(static_cast<Kotonoha_UpscalerPreset>(preset));
        config.dirtyConfig = true;
      }

      float scalerFactor = config.scalerFactor.load();
      if (ImGui::SliderFloat("Upscale Factor", &scalerFactor, 1.0f, 4.0f,
                             "%.2fx")) {
        config.scalerFactor.store(scalerFactor);
        KotonohaUpscalingSetFactor(scalerFactor);
        config.dirtyConfig = true;
      }

      const char *upscalerName = Kotonoha_GetUpscalerName();
      const char *rendererName = debugUiGameContext->render != nullptr
                                     ? SDL_GetRendererName(debugUiGameContext->render)
                                     : nullptr;
      ImGui::TextDisabled("Renderer: %s",
                          rendererName != nullptr ? rendererName : "unavailable");
      if (!upscalingEnabled || scalerFactor <= 1.0f) {
        ImGui::TextDisabled("Upscale inactive");
      } else if (Kotonoha_GetUpscalerType() == KOTONOHA_UPSCALER_NONE) {
        ImGui::TextDisabled("Upscale: SDL fallback (%.2fx)", scalerFactor);
        ImGui::TextDisabled("Shader upscaling requires an OpenGL renderer after restart.");
      } else {
        ImGui::TextDisabled("Upscale: %s (%.2fx)",
                            upscalerName != nullptr ? upscalerName : "active",
                            scalerFactor);
      }
    }
    ImGui::Checkbox("Log", &logVisible);
    ImGui::Checkbox("About Kotonoha", &aboutVisible);
    if (extension != nullptr) {
      ImGui::Separator();
      extension(userdata, ImGui::GetCurrentContext());
    }
  }
  ImGui::End();
  UpdateFocusState();
}

bool ShouldCaptureEvent(const SDL_Event *event) {
  if (event == nullptr || !sdlBackendInitialized ||
      ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  const ImGuiIO &io = ImGui::GetIO();
  const char *focused = GetFocusedWindowName();
  const bool focusedWindowIsTool =
      focused != nullptr && std::strcmp(focused, "Debug") != 0;
  switch (event->type) {
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_WHEEL:
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_MOTION:
    return io.WantCaptureMouse;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
  case SDL_EVENT_TEXT_INPUT:
  case SDL_EVENT_TEXT_EDITING:
    // The always-open master panel keeps nav focus; it must not swallow keys.
    return io.WantTextInput || focusedWindowIsTool;
  default:
    return false;
  }
}

bool SyncActivation(Kotonoha_Game &context) {
  static bool wasEnabled = false;
  const bool enabled = context.config.enableDebugUi.load();
  const bool activated = enabled && !wasEnabled;
  wasEnabled = enabled;
  if (activated) {
    scriptEditorVisible = false;
    textEditorVisible = false;
    settingsVisible = false;
    aboutVisible = false;
    playlistVisible = false;
    playbackVisible = false;
    sceneLoaderVisible = false;
    logVisible = false;
    sceneBuffersVisible = false;
    canvasLayersVisible = false;
    errorPopupVisible = false;
    // Input meant for the game piles up while the UI is hidden; replaying it
    // on the first frame would drive keyboard nav (e.g. toggle a checkbox).
    if (ImGui::GetCurrentContext() != nullptr) {
      ImGuiIO &io = ImGui::GetIO();
      io.ClearEventsQueue();
      io.ClearInputKeys();
      io.ClearInputMouse();
    }
  }
  return activated;
}

bool &PlaybackVisibility() { return playbackVisible; }

bool DrawAuxiliaryWindows(::Kotonoha::Kotonoha *game,
                          ::Kotonoha::Gameplay *play,
                          Kotonoha_Game &context, bool forceOpenScenes,
                          bool &shouldExit, bool drawControlPanel) {
  (void)forceOpenScenes;
  if (sceneLoaderVisible) {
    DrawSceneLoaderWindow(game, context, shouldExit);
  }

  if (drawControlPanel) {
    DrawPlaybackWindow(game, play, context);
  }
  DrawPlaylistWindow(game, context);
  DrawSceneBuffersWindow(play);
  DrawCanvasLayersWindow(play);
  DrawEngineSettingsWindow(context);
  DrawAboutWindow();
  DrawLogPanel(&logVisible);
  if (!DrawScriptEditorWindow(game, context, play)) {
    return false;
  }
  return DrawTextEditorWindow(game, context, play);
}

bool Run(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
         Kotonoha_Game &context, bool forceOpenScenes) {
  if (game == nullptr) {
    return true;
  }

  SyncActivation(context);
  if (!context.config.enableDebugUi.load()) {
    return true;
  }

  const float configuredUiScale = context.config.debugUiScale.load();
  const float debugUiScale =
      (configuredUiScale >= 0.5f && configuredUiScale <= 3.0f)
          ? configuredUiScale
          : 1.0f;
  if (debugUiBaseStyleReady && appliedDebugUiScale != debugUiScale) {
    ImGuiStyle &style = ImGui::GetStyle();
    style = debugUiBaseStyle;
    style.ScaleAllSizes(debugUiScale);
    style.FontScaleMain = debugUiScale;
    appliedDebugUiScale = debugUiScale;
  }

  if (forceOpenScenes) {
    play = nullptr;
  } else {
    TryAutoLoadCurrentScene(game, context);
  }

  // A game may supply a live Gameplay that is intentionally not registered in
  // Kotonoha::gameplays (temporary playback, preview, replay, etc.).  Preserve
  // that explicit target.  Fall back to the registry only when the caller did
  // not provide one; otherwise debug windows silently inspect the wrong scene.
  if (!forceOpenScenes && play == nullptr) {
    if (context.config.scene < game->gameplays.size()) {
      play = game->gameplays[context.config.scene];
    }
  }

  BeginWorkspaceFrame(context);
  bool shouldExit = false;

  DrawPlaybackWindow(game, play, context);
  if (!DrawAuxiliaryWindows(game, play, context, forceOpenScenes,
                            shouldExit)) {
    return FinishWorkspaceFrame(context);
  }

  UpdateFocusState();
  return FinishWorkspaceFrame(context) && !shouldExit;
}

void ProcessEvent(SDL_Event *event) {
  if (sdlBackendInitialized && event != nullptr) {
    ImGui_ImplSDL3_ProcessEvent(event);
  }
}

} // namespace Kotonoha::Debug
