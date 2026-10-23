#include <Kotonoha/debug/DebugInternal.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/debug/LogPanel.hpp>
#include <algorithm>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_internal.h>

namespace Kotonoha {

bool GetDisplayMode(const Kotonoha_Game &context, DisplayMode *mode) {
  if (context.window == nullptr || mode == nullptr) {
    SDL_SetError("Display mode query requires a window and output value");
    return false;
  }

  const SDL_WindowFlags flags = SDL_GetWindowFlags(context.window);
  if ((flags & SDL_WINDOW_FULLSCREEN) == 0) {
    *mode = DisplayMode::Windowed;
  } else {
    *mode = SDL_GetWindowFullscreenMode(context.window) != nullptr
                ? DisplayMode::ExclusiveFullscreen
                : DisplayMode::BorderlessFullscreen;
  }
  return true;
}

bool SetDisplay(Kotonoha_Game &context, DisplayMode mode,
                SDL_DisplayID targetDisplay) {
  if (mode != DisplayMode::Windowed &&
      mode != DisplayMode::ExclusiveFullscreen &&
      mode != DisplayMode::BorderlessFullscreen) {
    SDL_SetError("Invalid Kotonoha display mode");
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Kotonoha::SetDisplay failed: %s",
                 SDL_GetError());
    return false;
  }

  if (context.window == nullptr) {
    SDL_SetError("Display mode change requires a window");
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Kotonoha::SetDisplay failed: %s",
                 SDL_GetError());
    return false;
  }

  SDL_Window *window = context.window;
  const SDL_DisplayID currentDisplay = SDL_GetDisplayForWindow(window);
  if (targetDisplay == 0) {
    targetDisplay = currentDisplay;
  }
  if (targetDisplay == 0) {
    SDL_SetError("No display is available for the window");
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Kotonoha::SetDisplay failed: %s",
                 SDL_GetError());
    return false;
  }

  SDL_Rect targetBounds{};
  if (!SDL_GetDisplayBounds(targetDisplay, &targetBounds)) {
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                 "Kotonoha::SetDisplay could not query display bounds: %s",
                 SDL_GetError());
    return false;
  }

  DisplayMode currentMode;
  if (!GetDisplayMode(context, &currentMode)) {
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                 "Kotonoha::SetDisplay could not query current mode: %s",
                 SDL_GetError());
    return false;
  }

  auto syncActualState = [&]() {
    context.flags = SDL_GetWindowFlags(window);
    DisplayMode actualMode;
    if (GetDisplayMode(context, &actualMode)) {
      context.config.fullScreen = actualMode != DisplayMode::Windowed;
    }
  };
  auto fail = [&](const char *operation) {
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Kotonoha::SetDisplay failed to %s: %s",
                 operation, SDL_GetError());
    syncActualState();
    return false;
  };

  if (currentMode == mode && currentDisplay == targetDisplay) {
    syncActualState();
    return true;
  }

  if (currentMode != DisplayMode::Windowed) {
    if (!SDL_SetWindowFullscreen(window, false)) {
      return fail("leave fullscreen");
    }
    if (!SDL_SyncWindow(window)) {
      return fail("synchronize the window after leaving fullscreen");
    }
  }

  if (targetDisplay != currentDisplay) {
    if (!SDL_SetWindowPosition(window, targetBounds.x + 50,
                               targetBounds.y + 50)) {
      return fail("move the window to the requested display");
    }
    if (!SDL_SyncWindow(window)) {
      return fail("synchronize the window on the requested display");
    }
  }

  switch (mode) {
  case DisplayMode::Windowed:
    if (!SDL_SetWindowFullscreenMode(window, nullptr)) {
      return fail("reset the fullscreen mode");
    }
    if (!SDL_SetWindowBordered(window, true)) {
      return fail("restore window decorations");
    }
    if (!SDL_SetWindowSize(window, context.config.windowSize[0],
                           context.config.windowSize[1])) {
      return fail("restore the configured window size");
    }
    break;

  case DisplayMode::ExclusiveFullscreen: {
    const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(targetDisplay);
    if (desktopMode == nullptr) {
      return fail("query the desktop display mode");
    }

    SDL_DisplayMode fullscreenMode{};
    SDL_ClearError();
    if (!SDL_GetClosestFullscreenDisplayMode(
            targetDisplay, desktopMode->w, desktopMode->h,
            desktopMode->refresh_rate, true, &fullscreenMode)) {
      if (SDL_GetError()[0] == '\0') {
        SDL_SetError("No compatible exclusive fullscreen mode is available");
      }
      return fail("find an exclusive fullscreen mode");
    }
    if (!SDL_SetWindowFullscreenMode(window, &fullscreenMode)) {
      return fail("set the exclusive fullscreen mode");
    }
    if (!SDL_SetWindowFullscreen(window, true)) {
      return fail("enter exclusive fullscreen");
    }
    break;
  }

  case DisplayMode::BorderlessFullscreen:
    if (!SDL_SetWindowFullscreenMode(window, nullptr)) {
      return fail("select borderless fullscreen");
    }
    if (!SDL_SetWindowFullscreen(window, true)) {
      return fail("enter borderless fullscreen");
    }
    break;
  }

  if (!SDL_SyncWindow(window)) {
    return fail("synchronize the requested display mode");
  }

  DisplayMode actualMode;
  if (!GetDisplayMode(context, &actualMode)) {
    return fail("verify the requested display mode");
  }
  if (actualMode != mode) {
    SDL_SetError("The window manager rejected the requested display mode");
    return fail("apply the requested display mode");
  }

  syncActualState();
  return true;
}

} // namespace Kotonoha

namespace Kotonoha::Debug {

namespace {

ImGuiStyle debugUiBaseStyle;
bool debugUiBaseStyleReady = false;
float appliedDebugUiScale = 1.0f;
bool sdlBackendInitialized = false;
bool rendererBackendInitialized = false;

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
  anyWindowFocused = GetFocusedWindowName() != nullptr;
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
      mainFont = io.Fonts->AddFontFromMemoryTTF(
          fontData, static_cast<int>(fontDataSize), 16.0f);
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

void DrawWindowMenu(WindowMenuExtension extension, void *userdata) {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }
  if (ImGui::BeginMenu("Debug")) {
    ImGui::MenuItem("Load Scenes", nullptr, &openScenesVisible);
    ImGui::MenuItem("Control Panel", nullptr, &controlPanelVisible);
    ImGui::MenuItem("Playlist", nullptr, &playlistVisible);
    ImGui::MenuItem("Script Editor", nullptr, &scriptEditorVisible);
    ImGui::MenuItem("Text Editor", nullptr, &textEditorVisible);
    ImGui::MenuItem("Scene Buffers", nullptr, &sceneBuffersVisible);
    ImGui::MenuItem("Canvas Layers", nullptr, &canvasLayersVisible);
    ImGui::MenuItem("Settings", nullptr, &settingsVisible);
    ImGui::MenuItem("Log", nullptr, &logVisible);
    ImGui::MenuItem("About", nullptr, &aboutVisible);
    if (extension != nullptr) {
      ImGui::Separator();
      extension(userdata);
    }
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

static void UpdateTitleScenesWindow(bool forceOpenScenes) {
  if (forceOpenScenes) {
    if (!titleScenesWindowSession) {
      openScenesVisible = true;
      titleScenesWindowSession = true;
    }
  } else if (titleScenesWindowSession) {
    openScenesVisible = false;
    titleScenesWindowSession = false;
  }
}

bool &ControlPanelVisibility() { return controlPanelVisible; }

bool DrawAuxiliaryWindows(::Kotonoha::Kotonoha *game,
                          ::Kotonoha::Gameplay *play,
                          Kotonoha_Game &context, bool forceOpenScenes,
                          bool &shouldExit, bool drawControlPanel) {
  UpdateTitleScenesWindow(forceOpenScenes);
  if (openScenesVisible) {
    DrawOpenScenesWindow(game, context, shouldExit);
  }

  if (drawControlPanel) {
    DrawControlPanel(game, play, context);
  }
  DrawPlaylistWindow(game, context);
  DrawSceneBuffersWindow(play);
  DrawCanvasLayersWindow(play);
  DrawSettingsWindow(context);
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

  UpdateTitleScenesWindow(forceOpenScenes);
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

  if (!forceOpenScenes && context.config.scene < game->gameplays.size()) {
    play = game->gameplays[context.config.scene];
  } else if (!forceOpenScenes) {
    play = nullptr;
  }

  BeginWorkspaceFrame(context);
  bool shouldExit = false;

  DrawControlPanel(game, play, context);
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
