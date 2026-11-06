#include "ImGuiTheme.hpp"

#include <Kotonoha/core/FileSystem.hpp>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <vector>

namespace Kotonoha::ImGuiTheme {
namespace {
constexpr ImVec4 Accent{0.75f, 0.43f, 0.95f, 1.0f};
constexpr ImVec4 Success{0.36f, 0.84f, 0.55f, 1.0f};
constexpr ImVec4 Warning{1.00f, 0.62f, 0.34f, 1.0f};
constexpr ImVec4 Error{1.00f, 0.38f, 0.42f, 1.0f};
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
      // A bad optional UI font must not make the engine unusable. The normal
      // settings validation/reporting path still exposes the bad path.
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
  style.WindowPadding = {16.0f * scale, 14.0f * scale};
  style.FramePadding = {16.0f * scale, 8.0f * scale};
  style.ItemSpacing = {10.0f * scale, 8.0f * scale};
  style.ItemInnerSpacing = {8.0f * scale, 6.0f * scale};
  style.ScrollbarSize = 16.0f * scale;
  style.GrabMinSize = 12.0f * scale;
  style.WindowRounding = 8.0f * scale;
  style.ChildRounding = 6.0f * scale;
  style.FrameRounding = 5.0f * scale;
  style.PopupRounding = 5.0f * scale;
  style.ScrollbarRounding = 8.0f * scale;
  style.GrabRounding = 4.0f * scale;
  style.TabRounding = 5.0f * scale;
  style.FontSizeBase = 16.0f * scale;
  style.Colors[ImGuiCol_CheckMark] = Accent;
  style.Colors[ImGuiCol_SliderGrab] = Accent;
  style.Colors[ImGuiCol_SliderGrabActive] = {0.88f, 0.63f, 1.0f, 1.0f};
  style.Colors[ImGuiCol_ButtonActive] = {0.42f, 0.20f, 0.57f, 1.0f};
  style.Colors[ImGuiCol_HeaderActive] = {0.42f, 0.20f, 0.57f, 1.0f};
  style.Colors[ImGuiCol_TabSelected] = {0.35f, 0.18f, 0.48f, 1.0f};
}

ImVec4 accent() { return Accent; }
ImVec4 success() { return Success; }
ImVec4 warning() { return Warning; }
ImVec4 error() { return Error; }
ImVec4 muted() { return {0.58f, 0.58f, 0.64f, 1.0f}; }

void statusText(const char *text, bool error) {
  ImGui::TextColored(error ? Error : Success, "%s", text ? text : "");
}

bool primaryButton(const char *label, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.38f, 0.18f, 0.52f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.50f, 0.25f, 0.67f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.60f, 0.32f, 0.77f, 1.0f));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(3);
  return pressed;
}

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
