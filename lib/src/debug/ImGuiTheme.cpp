#include "ImGuiTheme.hpp"

#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace Kotonoha::ImGuiTheme {
struct ShaderFilePicker::Result {
  std::mutex mutex;
  bool done = false;
  std::vector<std::string> files;
  std::string error;
};

void SDLCALL ShaderFilePicker::finished(void *data, const char *const *files,
                                        int) {
  std::unique_ptr<std::shared_ptr<Result>> owner(
      static_cast<std::shared_ptr<Result> *>(data));
  std::lock_guard lock((*owner)->mutex);
  if (!files)
    (*owner)->error = SDL_GetError();
  else
    for (size_t i = 0; files[i]; ++i)
      (*owner)->files.emplace_back(files[i]);
  (*owner)->done = true;
}

void ShaderFilePicker::browse(SDL_Window *window) {
  if (pending)
    return;
  failure.clear();
  pending = std::make_shared<Result>();
  static const SDL_DialogFileFilter filters[]{{"MPV GLSL shaders", "glsl"}};
  SDL_ShowOpenFileDialog(finished, new std::shared_ptr<Result>(pending), window,
                         filters, 1, nullptr, true);
}

bool ShaderFilePicker::poll(UpscaleSettings &settings) {
  if (!pending)
    return false;
  std::vector<std::string> files;
  {
    std::lock_guard lock(pending->mutex);
    if (!pending->done)
      return false;
    files = pending->files;
    failure = pending->error;
  }
  pending.reset();
  if (!failure.empty())
    SDL_LogError(LogConfig, "Shader file dialog: %s", failure.c_str());
  if (files.empty())
    return false; // Cancellation leaves the current list intact.
  settings.shaderFiles = UpscaleShaderFileList(files);
  return true;
}

bool ShaderFilePicker::draw(SDL_Window *window, UpscaleSettings &settings) {
  bool changed = poll(settings);
  ImGui::TextUnformatted("MPV shader files (executed in list order)");
  if (ImGui::Button("Choose shader files..."))
    browse(window);
  ImGui::SameLine();
  if (ImGui::Button("Use preset")) {
    settings.shaderFiles.clear();
    changed = true;
  }
  auto files = UpscaleShaderFiles(settings.shaderFiles);
  for (size_t i = 0; i < files.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::SmallButton("Remove")) {
      files.erase(files.begin() + static_cast<std::ptrdiff_t>(i));
      settings.shaderFiles = UpscaleShaderFileList(files);
      changed = true;
      ImGui::PopID();
      break;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(i == 0);
    if (ImGui::SmallButton("Up")) {
      std::swap(files[i - 1], files[i]);
      settings.shaderFiles = UpscaleShaderFileList(files);
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted(files[i].c_str());
    ImGui::PopID();
  }
  if (ImGui::TreeNode("Bundled shader files")) {
    std::vector<std::string> bundled;
    for (const auto *folder : {"Anime4K", "ArtCNN"}) {
      const auto directory = FileResolver::engineAsset(folder);
      for (const auto &entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == ".glsl")
          bundled.push_back(entry.path().string());
    }
    std::sort(bundled.begin(), bundled.end());
    for (const auto &path : bundled) {
      const auto found = std::find(files.begin(), files.end(), path);
      bool selected = found != files.end();
      if (ImGui::Checkbox(
              std::filesystem::path(path).filename().string().c_str(),
              &selected)) {
        if (selected)
          files.push_back(path);
        else
          files.erase(found);
        settings.shaderFiles = UpscaleShaderFileList(files);
        changed = true;
      }
    }
    ImGui::TreePop();
  }
  ImGui::TextDisabled("One path per line. Empty list uses the preset.");
  if (!failure.empty())
    statusText(failure.c_str(), true);
  return changed;
}

namespace {

constexpr float FontSize = 15.0f;

ImFont *addFontMemory(const std::string &fontPath, bool merge) {
  try {
    auto data = FileResolver::loadFontMemory(fontPath);
    if (data.empty())
      return nullptr;
    ImFontConfig config;
    config.MergeMode = merge;
    config.Flags |= ImFontFlags_NoLoadError;
    config.OversampleH = 2;
    void *mem = IM_ALLOC(data.size());
    std::memcpy(mem, data.data(), data.size());
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        mem, static_cast<int>(data.size()), FontSize, &config);
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "ImGui font memory load error: %s", error.what());
    return nullptr;
  }
}

ImVec4 rgba(float r, float g, float b, float a = 1) { return {r, g, b, a}; }

} // namespace

void loadFonts(const std::string &fontPath) {
  // Engine fonts loaded into memory for ImGui, SDL_ttf, and libass.
  auto &io = ImGui::GetIO();
  ImFont *font = nullptr;
  if (!fontPath.empty()) {
    font = addFontMemory(fontPath, false);
  }
  if (!font) {
    font = addFontMemory("", false);
  }
  if (!font) {
    ImFontConfig config;
    config.SizePixels = FontSize;
    font = io.Fonts->AddFontDefault(&config);
  }
  io.FontDefault = font;
}

void apply(float scale) {
  auto &style = ImGui::GetStyle();
  style = ImGuiStyle{};
  ImGui::StyleColorsDark(&style);
  style.FontSizeBase = FontSize;

  style.WindowRounding = 10;
  style.ChildRounding = 8;
  style.FrameRounding = 6;
  style.PopupRounding = 8;
  style.ScrollbarRounding = 10;
  style.GrabRounding = 6;
  style.TabRounding = 6;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 0;
  style.TabBarBorderSize = 1;
  style.TabBarOverlineSize = 2;
  style.SeparatorTextBorderSize = 2;
  style.WindowPadding = {12, 12};
  style.FramePadding = {8, 5};
  style.CellPadding = {6, 4};
  style.ItemSpacing = {8, 7};
  style.ItemInnerSpacing = {6, 4};
  style.ScrollbarSize = 12;
  style.GrabMinSize = 12;
  style.WindowTitleAlign = {0.5f, 0.5f};
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.SeparatorTextAlign = {0, 0.5f};
  style.SeparatorTextPadding = {14, 4};

  auto *c = style.Colors;
  c[ImGuiCol_Text] = rgba(.94f, .92f, .98f);
  c[ImGuiCol_TextDisabled] = rgba(.54f, .48f, .62f);
  c[ImGuiCol_WindowBg] = rgba(.09f, .07f, .13f, .96f);
  c[ImGuiCol_ChildBg] = rgba(.07f, .05f, .10f, .80f);
  c[ImGuiCol_PopupBg] = rgba(.11f, .08f, .16f, .98f);
  c[ImGuiCol_Border] = rgba(.28f, .18f, .42f, .60f);
  c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = rgba(.16f, .11f, .24f);
  c[ImGuiCol_FrameBgHovered] = rgba(.24f, .15f, .36f);
  c[ImGuiCol_FrameBgActive] = rgba(.30f, .18f, .45f);
  c[ImGuiCol_TitleBg] = rgba(.12f, .08f, .18f);
  c[ImGuiCol_TitleBgActive] = rgba(.24f, .12f, .36f);
  c[ImGuiCol_TitleBgCollapsed] = rgba(.08f, .05f, .12f, .75f);
  c[ImGuiCol_MenuBarBg] = rgba(.12f, .08f, .18f);
  c[ImGuiCol_ScrollbarBg] = rgba(.08f, .05f, .12f, .60f);
  c[ImGuiCol_ScrollbarGrab] = rgba(.24f, .14f, .36f);
  c[ImGuiCol_ScrollbarGrabHovered] = rgba(.36f, .20f, .52f);
  c[ImGuiCol_ScrollbarGrabActive] = rgba(.48f, .25f, .68f);
  c[ImGuiCol_CheckMark] = rgba(.80f, .45f, .95f);
  c[ImGuiCol_SliderGrab] = rgba(.56f, .28f, .82f);
  c[ImGuiCol_SliderGrabActive] = rgba(.68f, .34f, .98f);
  c[ImGuiCol_Button] = rgba(.32f, .16f, .48f, .85f);
  c[ImGuiCol_ButtonHovered] = rgba(.42f, .22f, .62f);
  c[ImGuiCol_ButtonActive] = rgba(.50f, .26f, .72f);
  c[ImGuiCol_Header] = rgba(.28f, .15f, .42f, .75f);
  c[ImGuiCol_HeaderHovered] = rgba(.38f, .20f, .56f, .85f);
  c[ImGuiCol_HeaderActive] = rgba(.46f, .24f, .68f);
  c[ImGuiCol_Separator] = rgba(.28f, .18f, .42f, .60f);
  c[ImGuiCol_SeparatorHovered] = rgba(.45f, .24f, .65f, .80f);
  c[ImGuiCol_SeparatorActive] = rgba(.60f, .30f, .85f);
  c[ImGuiCol_ResizeGrip] = rgba(.42f, .22f, .62f, .25f);
  c[ImGuiCol_ResizeGripHovered] = rgba(.56f, .28f, .82f, .65f);
  c[ImGuiCol_ResizeGripActive] = rgba(.68f, .34f, .98f, .90f);
  c[ImGuiCol_InputTextCursor] = rgba(.85f, .65f, .98f);
  c[ImGuiCol_Tab] = rgba(.16f, .10f, .24f);
  c[ImGuiCol_TabHovered] = rgba(.42f, .22f, .62f);
  c[ImGuiCol_TabSelected] = rgba(.32f, .16f, .48f);
  c[ImGuiCol_TabSelectedOverline] = rgba(.80f, .45f, .95f);
  c[ImGuiCol_TabDimmed] = rgba(.12f, .08f, .18f);
  c[ImGuiCol_TabDimmedSelected] = rgba(.24f, .13f, .36f);
  c[ImGuiCol_TabDimmedSelectedOverline] = rgba(.50f, .30f, .65f);
  c[ImGuiCol_PlotLines] = rgba(.80f, .60f, .95f);
  c[ImGuiCol_PlotLinesHovered] = rgba(.95f, .50f, .95f);
  c[ImGuiCol_PlotHistogram] = rgba(.56f, .28f, .82f);
  c[ImGuiCol_PlotHistogramHovered] = rgba(.68f, .34f, .98f);
  c[ImGuiCol_TableHeaderBg] = rgba(.16f, .10f, .24f);
  c[ImGuiCol_TableBorderStrong] = rgba(.32f, .20f, .46f);
  c[ImGuiCol_TableBorderLight] = rgba(.22f, .15f, .32f);
  c[ImGuiCol_TableRowBgAlt] = rgba(1, 1, 1, .03f);
  c[ImGuiCol_TextLink] = rgba(.85f, .65f, .98f);
  c[ImGuiCol_TextSelectedBg] = rgba(.48f, .22f, .68f, .45f);
  c[ImGuiCol_DragDropTarget] = rgba(.95f, .50f, .95f, .90f);
  c[ImGuiCol_NavCursor] = rgba(.80f, .45f, .95f);
  c[ImGuiCol_NavWindowingHighlight] = rgba(.95f, .80f, 1, .70f);
  c[ImGuiCol_NavWindowingDimBg] = rgba(.10f, .05f, .15f, .35f);
  c[ImGuiCol_ModalWindowDimBg] = rgba(.05f, .02f, .08f, .55f);

  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;
}

ImVec4 accent() { return rgba(.85f, .65f, .98f); }
ImVec4 muted() { return rgba(.62f, .56f, .70f); }
ImVec4 success() { return rgba(.52f, .90f, .66f); }
ImVec4 warning() { return rgba(1, .80f, .40f); }
ImVec4 error() { return rgba(1, .45f, .52f); }

void statusText(const char *text, bool isError) {
  ImGui::PushStyleColor(ImGuiCol_Text, isError ? error() : success());
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool primaryButton(const char *label, const ImVec2 &size) {
  ImGui::PushStyleColor(ImGuiCol_Button, rgba(.56f, .28f, .82f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba(.66f, .36f, .92f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgba(.74f, .42f, 1));
  ImGui::PushStyleColor(ImGuiCol_Text, rgba(1, 1, 1));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return pressed;
}

} // namespace Kotonoha::ImGuiTheme
