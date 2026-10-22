#include <Kotonoha/debug/DebugInternal.hpp>
#include <Kotonoha/debug/DebugUi.hpp>
#include <Kotonoha/debug/LogPanel.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_internal.h>

namespace Kotonoha::Debug {

namespace {

ImGuiStyle debugUiBaseStyle;
bool debugUiBaseStyleReady = false;
float appliedDebugUiScale = 1.0f;

} // namespace

bool IsAnyWindowFocused() { return anyWindowFocused; }

bool IsMouseCaptured() {
  return ImGui::GetCurrentContext() != nullptr &&
         ImGui::GetIO().WantCaptureMouse;
}

bool IsScriptEditorVisible() { return scriptEditorVisible; }

static bool FinishFrame(Kotonoha_Game &context) {
  ImGui::Render();
  const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
  SDL_SetRenderScale(context.render, fbScale.x, fbScale.y);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), context.render);
  SDL_SetRenderScale(context.render, 1.0f, 1.0f);
  return true;
}

static const char *GetFocusedWindowName() {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  return (ctx && ctx->NavWindow) ? ctx->NavWindow->Name : nullptr;
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

void Init(Kotonoha_Game &gameContext, std::vector<Uint8> font) {
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

  ImGui_ImplSDL3_InitForSDLRenderer(gameContext.window, gameContext.render);
  ImGui_ImplSDLRenderer3_Init(gameContext.render);
}

bool Run(::Kotonoha::Kotonoha *game, ::Kotonoha::Gameplay *play,
         Kotonoha_Game &context) {
  if (!context.config.enableDebugUi || game == nullptr) {
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

  TryAutoLoadCurrentScene(game, context);

  if (context.config.scene < game->gameplays.size()) {
    play = game->gameplays[context.config.scene];
  } else {
    play = nullptr;
  }

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  bool shouldExit = false;

  if (play != nullptr) {
    DrawControlPanel(game, play, context);
  } else {
    DrawOpenScenesWindow(game, context, shouldExit);
  }

  DrawPlaylistWindow(game, context);
  DrawSceneBuffersWindow(play);
  DrawCanvasLayersWindow(play);
  DrawSettingsWindow(context);
  DrawAboutWindow();
  DrawLogPanel(&logVisible);

  if (!DrawScriptEditorWindow(game, context)) {
    return FinishFrame(context);
  }

  if (!DrawTextEditorWindow(game, context)) {
    return FinishFrame(context);
  }

  anyWindowFocused = (GetFocusedWindowName() != nullptr);
  return !shouldExit && FinishFrame(context);
}

void ProcessEvent(SDL_Event *event) { ImGui_ImplSDL3_ProcessEvent(event); }

} // namespace Kotonoha::Debug
