#include "ImGuiTheme.hpp"

#include <Kotonoha/core/FileSystem.hpp>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <vector>

namespace Kotonoha::ImGuiTheme {
namespace {
// Paleta "noir" com acento roxo-violeta — coesa, baixa saturação nos fundos
// e contraste alto no texto/acentos.
constexpr ImVec4 Accent{0.85f, 0.55f, 1.00f, 1.00f};
constexpr ImVec4 AccentDim{0.55f, 0.30f, 0.70f, 1.00f};
constexpr ImVec4 Success{0.40f, 0.88f, 0.60f, 1.00f};
constexpr ImVec4 Warning{1.00f, 0.70f, 0.35f, 1.00f};
constexpr ImVec4 Error{1.00f, 0.45f, 0.50f, 1.00f};
constexpr ImVec4 WindowBg{0.085f, 0.075f, 0.115f, 1.00f};
constexpr ImVec4 ChildBg{0.105f, 0.095f, 0.140f, 1.00f};
constexpr ImVec4 PopupBg{0.085f, 0.075f, 0.115f, 1.00f};
constexpr ImVec4 Border{0.24f, 0.19f, 0.32f, 1.00f};
constexpr ImVec4 BorderHovered{0.42f, 0.32f, 0.52f, 1.00f};
constexpr ImVec4 FrameBg{0.15f, 0.13f, 0.20f, 1.00f};
constexpr ImVec4 FrameBgHovered{0.22f, 0.18f, 0.28f, 1.00f};
constexpr ImVec4 FrameBgActive{0.30f, 0.24f, 0.36f, 1.00f};
constexpr ImVec4 TitleBg{0.115f, 0.100f, 0.155f, 1.00f};
constexpr ImVec4 TitleBgActive{0.15f, 0.125f, 0.20f, 1.00f};
constexpr ImVec4 TitleBgCollapsed{0.085f, 0.075f, 0.115f, 1.00f};
constexpr ImVec4 MenuBarBg{0.105f, 0.095f, 0.140f, 1.00f};
constexpr ImVec4 ScrollbarBg{0.105f, 0.095f, 0.140f, 1.00f};
constexpr ImVec4 ScrollbarGrab{0.30f, 0.24f, 0.38f, 1.00f};
constexpr ImVec4 ScrollbarGrabHovered{0.44f, 0.34f, 0.52f, 1.00f};
constexpr ImVec4 ScrollbarGrabActive{0.58f, 0.44f, 0.68f, 1.00f};
constexpr ImVec4 Header{0.19f, 0.15f, 0.26f, 1.00f};
constexpr ImVec4 HeaderHovered{0.30f, 0.23f, 0.38f, 1.00f};
constexpr ImVec4 HeaderActive{0.42f, 0.30f, 0.52f, 1.00f};
constexpr ImVec4 Separator{0.24f, 0.19f, 0.32f, 1.00f};
constexpr ImVec4 SeparatorHovered{0.40f, 0.30f, 0.48f, 1.00f};
constexpr ImVec4 SeparatorActive{0.52f, 0.40f, 0.60f, 1.00f};
constexpr ImVec4 Text{0.96f, 0.96f, 1.00f, 1.00f};
constexpr ImVec4 TextDisabled{0.55f, 0.55f, 0.65f, 1.00f};
constexpr ImVec4 TextSelectedBg{0.45f, 0.30f, 0.60f, 0.35f};
constexpr ImVec4 Button{0.22f, 0.14f, 0.32f, 1.00f};
constexpr ImVec4 ButtonHovered{0.36f, 0.24f, 0.50f, 1.00f};
constexpr ImVec4 ButtonActive{0.52f, 0.34f, 0.68f, 1.00f};
} // namespace

void loadFonts(const std::string &fontPath) {
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  ImFont *font = nullptr;
  if (!fontPath.empty()) {
    try {
      const auto resolved = FileResolver::font(fontPath);
      if (std::filesystem::exists(resolved))
        font = io.Fonts->AddFontFromFileTTF(resolved.string().c_str(), 18.0f);
    } catch (...) {
      // A bad optional UI font must not make the engine unusable.
    }
  }
  if (!font)
    font = io.Fonts->AddFontDefault();
  io.FontDefault = font;
}

void apply(float value) {
  const float scale = std::clamp(value, 0.5f, 4.0f);
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();

  // Geometry (scales with UI scale).
  style.WindowPadding = {16.0f * scale, 14.0f * scale};
  style.WindowRounding = 10.0f * scale;
  style.WindowBorderSize = 1.0f * scale;
  style.ChildRounding = 8.0f * scale;
  style.ChildBorderSize = 1.0f * scale;
  style.PopupRounding = 8.0f * scale;
  style.PopupBorderSize = 1.0f * scale;
  style.FramePadding = {14.0f * scale, 7.0f * scale};
  style.FrameRounding = 6.0f * scale;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = {10.0f * scale, 7.0f * scale};
  style.ItemInnerSpacing = {7.0f * scale, 5.0f * scale};
  style.IndentSpacing = 16.0f * scale;
  style.ScrollbarSize = 14.0f * scale;
  style.ScrollbarRounding = 7.0f * scale;
  style.GrabMinSize = 14.0f * scale;
  style.GrabRounding = 4.0f * scale;
  style.TabRounding = 6.0f * scale;
  style.SeparatorTextBorderSize = 1.0f * scale;
  style.SeparatorTextPadding = {24.0f * scale, 4.0f * scale};
  style.ButtonTextAlign = {0.5f, 0.5f};
  style.DisplayWindowPadding = {24.0f * scale, 24.0f * scale};
  style.DisplaySafeAreaPadding = {4.0f * scale, 4.0f * scale};
  style.FontSizeBase = 16.0f * scale;

  // Colors.
  style.Colors[ImGuiCol_Text] = Text;
  style.Colors[ImGuiCol_TextDisabled] = TextDisabled;
  style.Colors[ImGuiCol_TextSelectedBg] = TextSelectedBg;
  style.Colors[ImGuiCol_WindowBg] = WindowBg;
  style.Colors[ImGuiCol_ChildBg] = ChildBg;
  style.Colors[ImGuiCol_PopupBg] = PopupBg;
  style.Colors[ImGuiCol_Border] = Border;
  style.Colors[ImGuiCol_BorderShadow] = {0.0f, 0.0f, 0.0f, 0.45f};
  style.Colors[ImGuiCol_FrameBg] = FrameBg;
  style.Colors[ImGuiCol_FrameBgHovered] = FrameBgHovered;
  style.Colors[ImGuiCol_FrameBgActive] = FrameBgActive;
  style.Colors[ImGuiCol_TitleBg] = TitleBg;
  style.Colors[ImGuiCol_TitleBgActive] = TitleBgActive;
  style.Colors[ImGuiCol_TitleBgCollapsed] = TitleBgCollapsed;
  style.Colors[ImGuiCol_MenuBarBg] = MenuBarBg;
  style.Colors[ImGuiCol_ScrollbarBg] = ScrollbarBg;
  style.Colors[ImGuiCol_ScrollbarGrab] = ScrollbarGrab;
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = ScrollbarGrabHovered;
  style.Colors[ImGuiCol_ScrollbarGrabActive] = ScrollbarGrabActive;
  style.Colors[ImGuiCol_CheckMark] = Accent;
  style.Colors[ImGuiCol_SliderGrab] = Accent;
  style.Colors[ImGuiCol_SliderGrabActive] = AccentDim;
  style.Colors[ImGuiCol_Button] = Button;
  style.Colors[ImGuiCol_ButtonHovered] = ButtonHovered;
  style.Colors[ImGuiCol_ButtonActive] = ButtonActive;
  style.Colors[ImGuiCol_Header] = Header;
  style.Colors[ImGuiCol_HeaderHovered] = HeaderHovered;
  style.Colors[ImGuiCol_HeaderActive] = HeaderActive;
  style.Colors[ImGuiCol_Separator] = Separator;
  style.Colors[ImGuiCol_SeparatorHovered] = SeparatorHovered;
  style.Colors[ImGuiCol_SeparatorActive] = SeparatorActive;
  style.Colors[ImGuiCol_ResizeGrip] = {0.30f, 0.24f, 0.38f, 0.60f};
  style.Colors[ImGuiCol_ResizeGripHovered] = {0.44f, 0.34f, 0.52f, 0.70f};
  style.Colors[ImGuiCol_ResizeGripActive] = {0.58f, 0.44f, 0.68f, 0.80f};
  style.Colors[ImGuiCol_Tab] = Header;
  style.Colors[ImGuiCol_TabHovered] = HeaderHovered;
  style.Colors[ImGuiCol_PlotLines] = AccentDim;
  style.Colors[ImGuiCol_PlotLinesHovered] = Accent;
  style.Colors[ImGuiCol_PlotHistogram] = Accent;
  style.Colors[ImGuiCol_PlotHistogramHovered] = AccentDim;
  style.Colors[ImGuiCol_TableHeaderBg] = Header;
  style.Colors[ImGuiCol_TableBorderStrong] = BorderHovered;
  style.Colors[ImGuiCol_TableBorderLight] = Border;
  style.Colors[ImGuiCol_TableRowBg] = {0.0f, 0.0f, 0.0f, 0.0f};
  style.Colors[ImGuiCol_TableRowBgAlt] = {0.15f, 0.12f, 0.20f, 0.30f};
}

ImVec4 accent() { return Accent; }
ImVec4 success() { return Success; }
ImVec4 warning() { return Warning; }
ImVec4 error() { return Error; }
ImVec4 muted() { return TextDisabled; }

void statusText(const char *text, bool isError) {
  ImGui::TextColored(isError ? Error : Success, "%s", text ? text : "");
}

bool primaryButton(const char *label, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_Button, Button);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ButtonHovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ButtonActive);
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(3);
  return pressed;
}

// Desenha um cabeçalho de seção com um pequeno ponto de acento à esquerda.
void sectionHeader(const char *label) {
  ImGui::SeparatorText(label);
  const auto &style = ImGui::GetStyle();
  const float x = ImGui::GetCursorScreenPos().x - style.ItemSpacing.x;
  const float y = ImGui::GetCursorScreenPos().y -
                  ImGui::GetTextLineHeight() * 0.5f -
                  style.ItemSpacing.y * 0.5f;
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(x, y), ImVec2(x + 4.0f, y + ImGui::GetTextLineHeight()),
      ImGui::GetColorU32(Accent), 2.0f);
}

// Grupo com rótulo alinhado à esquerda e preenchimento à direita.
struct LabeledGroup {
  explicit LabeledGroup(const char *label) {
    ImGui::PushStyleColor(ImGuiCol_Text, TextDisabled);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginGroup();
  }
  ~LabeledGroup() { ImGui::EndGroup(); }
};

struct ShaderFilePicker::State {
  std::mutex mutex;
  bool done = false;
  std::vector<std::string> files;
  std::string error;
};

void SDLCALL ShaderFilePicker::finished(void *userdata,
                                        const char *const *files, int) {
  std::unique_ptr<std::shared_ptr<State>> owner(
      static_cast<std::shared_ptr<State> *>(userdata));
  auto state = *owner;
  std::lock_guard lock(state->mutex);
  if (!files) {
    state->error = SDL_GetError();
  } else {
    for (size_t i = 0; files[i]; ++i)
      state->files.emplace_back(files[i]);
  }
  state->done = true;
}

void ShaderFilePicker::browse(SDL_Window *window) {
  if (state)
    return;
  auto next = std::make_shared<State>();
  state = next;
  error.clear();
  static const SDL_DialogFileFilter filters[] = {
      {"MPV shader files", "glsl;hook"}, {"All files", "*"}};
  SDL_ShowOpenFileDialog(finished, new std::shared_ptr<State>(next), window,
                         filters, 2, nullptr, true);
}

void ShaderFilePicker::poll(UpscaleSettings &settings) {
  if (!state)
    return;
  bool done = false;
  std::vector<std::string> files;
  std::string failure;
  {
    std::lock_guard lock(state->mutex);
    done = state->done;
    if (done) {
      files = state->files;
      failure = state->error;
    }
  }
  if (!done)
    return;
  state.reset();
  error = std::move(failure);
  if (!error.empty() || files.empty())
    return;
  try {
    settings.shaderFiles = UpscaleShaderFileList(files);
  } catch (const std::exception &e) {
    error = e.what();
  }
}

bool ShaderFilePicker::draw(SDL_Window *window, UpscaleSettings &settings) {
  const std::string before = settings.shaderFiles;
  poll(settings);
  const auto files = UpscaleShaderFiles(settings.shaderFiles);
  ImGui::Text("Custom shaders: %zu", files.size());
  for (size_t i = 0; i < files.size(); ++i)
    ImGui::BulletText("%s", files[i].c_str());
  if (ImGui::Button("Choose shader files..."))
    browse(window);
  if (!files.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Clear shaders"))
      settings.shaderFiles.clear();
  }
  if (!error.empty())
    ImGui::TextColored(Error, "%s", error.c_str());
  return settings.shaderFiles != before;
}

} // namespace Kotonoha::ImGuiTheme
