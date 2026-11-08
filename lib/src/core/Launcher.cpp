#include <Kotonoha/core/Appearance.hpp>
#include <Kotonoha/core/AssetOverlay.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Launcher.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/media/AudioMixer.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>

#include "../debug/ImGuiTheme.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <stdexcept>

namespace Kotonoha {

struct Launcher::Dialog {
  std::mutex mutex;
  bool finished = false;
  std::string path;
  // SDL keeps pointers to the filters until the callback runs.
  std::vector<std::string> names, patterns;
  std::vector<SDL_DialogFileFilter> filters;
};

namespace {
constexpr const char *Modes[]{"Windowed", "Borderless fullscreen",
                              "Exclusive fullscreen"};

constexpr const char *Priorities[]{"Trace",   "Verbose", "Debug",   "Info",
                                   "Warning", "Error",   "Critical"};
constexpr std::pair<int, int> Resolutions[]{
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

const char *backendLabel(UpscaleBackend backend) {
  switch (backend) {
  case UpscaleBackend::None:
    return "None";
  case UpscaleBackend::Linear:
    return "Linear (SDL)";
  case UpscaleBackend::MPV:
    return "Anime4K / ArtCNN (OpenGL)";
  case UpscaleBackend::MetalFX:
    return "MetalFX";
  case UpscaleBackend::Auto:
    return "Automatic";
  }
  return "?";
}

// Backends tied to one renderer; the Engine falls back to Auto otherwise.
const char *requiredRenderer(UpscaleBackend backend,
                             const std::string &renderer) {
  if (backend == UpscaleBackend::MetalFX && !renderer.empty() &&
      renderer != "metal")
    return "metal";
  if (backend == UpscaleBackend::MPV && renderer.rfind("opengl", 0) != 0)
    return isMobilePlatform() ? "opengles2" : "opengl";
  return nullptr;
}
} // namespace

Launcher::Launcher(std::string name, std::string path, EngineSettings values)
    : title(std::move(name)), settingsPath(std::move(path)), draft(values),
      defaults(std::move(values)) {
  shaderPicker = std::make_unique<ImGuiTheme::ShaderFilePicker>();
  if (settingsPath.empty() || !std::filesystem::exists(settingsPath))
    return;
  try {
    draft = EngineSettings::load(settingsPath);
  } catch (const std::exception &error) {
    setStatus(std::string("Invalid settings, using defaults: ") + error.what(),
              true);
  }
}

Launcher::~Launcher() { close(); }

void Launcher::open() {
  if (isOpen())
    return;
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    throw std::runtime_error(SDL_GetError());
  videoInitialized = true;
  try {
    // 1. Define o modo padrão baseado na plataforma se não estiver configurado
    WindowMode mode = defaultWindowMode();

    // 2. Determina as flags da janela SDL3 com base no modo
    Uint32 windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (mode == WindowMode::Borderless || mode == WindowMode::Exclusive) {
      windowFlags |= SDL_WINDOW_FULLSCREEN;
    }

    // 3. Obtém o tamanho da tela do monitor principal se for mobile/fullscreen
    int width = 760;
    int height = 560;

    if (isMobilePlatform()) {
      SDL_DisplayID displayID = SDL_GetPrimaryDisplay();
      const SDL_DisplayMode *displayMode = SDL_GetCurrentDisplayMode(displayID);
      if (displayMode) {
        width = displayMode->w;
        height = displayMode->h;
      }
    }

    window.reset(SDL_CreateWindow(title.c_str(), width, height, windowFlags));
    if (!window)
      throw std::runtime_error(SDL_GetError());

    renderer.reset(SDL_CreateRenderer(window.get(), nullptr));
    if (!renderer)
      throw std::runtime_error(SDL_GetError());
    SDL_SetRenderVSync(renderer.get(), 1);

    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGuiTheme::loadFonts(draft.fontPath);
    ImGuiTheme::apply(1);

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get()))
      throw std::runtime_error("Cannot initialize ImGui for the launcher");
    if (!ImGui_ImplSDLRenderer3_Init(renderer.get())) {
      ImGui_ImplSDL3_Shutdown();
      throw std::runtime_error("Cannot initialize the launcher renderer");
    }
  } catch (...) {
    if (context) {
      ImGui::DestroyContext(context);
      context = nullptr;
    }
    renderer.reset();
    window.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    videoInitialized = false;
    throw;
  }
  SDL_LogInfo(LogCore, "Launcher opened (%s renderer)",
              SDL_GetRendererName(renderer.get()));
}

void Launcher::close() {
  if (context) {
    ImGui::SetCurrentContext(context);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context);
    context = nullptr;
  }
  renderer.reset();
  window.reset();
  if (videoInitialized) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    videoInitialized = false;
  }
}

bool Launcher::isOpen() const { return context != nullptr; }
Launcher::State Launcher::state() const { return current; }
void Launcher::setAudioChannels(std::vector<std::string> names) {
  std::vector<std::string> unique;
  for (auto &name : names) {
    AudioMixer::validateChannelName(name);
    if (std::find(unique.begin(), unique.end(), name) == unique.end())
      unique.push_back(std::move(name));
  }
  audioChannels = std::move(unique);
}

EngineSettings &Launcher::settings() { return draft; }
const std::string &Launcher::getSettingsPath() const { return settingsPath; }
const std::string &Launcher::getStatus() const { return status; }
void Launcher::quit() { current = State::Quit; }

void Launcher::drawGameOptions() {}
void Launcher::checkOverlay(bool force) {
  if (!force && overlayCheckedOnce && overlayChecked == draft.assetOverlayPath)
    return;
  overlayCheckedOnce = true;
  overlayChecked = draft.assetOverlayPath;
  overlayValid = true;
  overlayInfo.clear();
  try {
    AssetOverlay validateOverlay(draft.assetOverlayPath);
    overlayInfo = validateOverlay.root().empty()
                      ? "Automatic overlay: no overlay folder found"
                      : "Asset overlay: " + validateOverlay.root().string();
  } catch (const std::exception &error) {
    overlayValid = false;
    overlayInfo = error.what();
  }
}

void Launcher::selectOverlayFolder(const std::string &path) {
  draft.assetOverlayPath = path;
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    std::filesystem::path p(path);
    auto name =
        p.filename().empty() ? std::string("overlay") : p.filename().string();
    draft.assetOverlays[name] = path;
    draft.assetOverlayName = name;
  }
  checkOverlay(true);
}

bool Launcher::validate(std::string &error) {
  checkOverlay(true);
  if (!overlayValid) {
    error = overlayInfo;
    return false;
  }
  return true;
}
void Launcher::save() {}

void Launcher::setStatus(std::string message, bool error) {
  status = std::move(message);
  statusError = error;
  if (error && !status.empty())
    SDL_LogWarn(LogConfig, "Launcher: %s", status.c_str());
}

bool Launcher::launch() {
  try {
    draft.validate();
    std::string error;
    if (!validate(error)) {
      setStatus(error.empty() ? "Invalid settings" : error, true);
      return false;
    }
    if (!settingsPath.empty())
      draft.save(settingsPath);
    save();
  } catch (const std::exception &error) {
    setStatus(error.what(), true);
    return false;
  }
  setStatus("", false);
  current = State::Launch;
  SDL_LogInfo(LogConfig, "Launcher: starting with upscaling %s x%.2f",
              UpscaleBackendName(draft.upscaling.backend),
              draft.upscaling.factor);
  return true;
}

void SDLCALL Launcher::dialogFinished(void *data, const char *const *files,
                                      int) {
  std::unique_ptr<std::shared_ptr<Dialog>> state(
      static_cast<std::shared_ptr<Dialog> *>(data));
  std::lock_guard lock((*state)->mutex);
  if (files && files[0])
    (*state)->path = files[0];
  else if (!files)
    SDL_LogWarn(LogConfig, "File dialog: %s", SDL_GetError());
  (*state)->finished = true;
}

void Launcher::browseFile(const FileFilters &filters,
                          const std::string &initial,
                          std::function<void(const std::string &)> done) {
  if (dialog)
    return;
  auto next = std::make_shared<Dialog>();
  next->names.reserve(filters.size());
  next->patterns.reserve(filters.size());
  next->filters.reserve(filters.size());
  for (const auto &[name, pattern] : filters) {
    next->names.push_back(name);
    next->patterns.push_back(pattern);
  }
  for (std::size_t i = 0; i < filters.size(); ++i)
    next->filters.push_back(
        {next->names[i].c_str(), next->patterns[i].c_str()});
  dialog = next;
  dialogDone = std::move(done);
  // The callback may run on another thread and after the launcher is gone,
  // so it owns a reference to the shared state.
  SDL_ShowOpenFileDialog(dialogFinished, new std::shared_ptr<Dialog>(next),
                         window.get(), next->filters.data(),
                         static_cast<int>(next->filters.size()),
                         initial.empty() ? nullptr : initial.c_str(), false);
}

void Launcher::browseFolder(const std::string &initial,
                            std::function<void(const std::string &)> done) {
  if (dialog)
    return;
  auto next = std::make_shared<Dialog>();
  dialog = next;
  dialogDone = std::move(done);
  SDL_ShowOpenFolderDialog(dialogFinished, new std::shared_ptr<Dialog>(next),
                           window.get(),
                           initial.empty() ? nullptr : initial.c_str(), false);
}

SDL_AppResult Launcher::event(const SDL_Event &event) {
  if (!isOpen())
    return SDL_APP_CONTINUE;
  ImGui::SetCurrentContext(context);
  ImGui_ImplSDL3_ProcessEvent(&event);
  SDL_Event pointer{};
  if (touchPointer.translate(event, *window, pointer)) {
    SDL_Event position{};
    position.type = SDL_EVENT_MOUSE_MOTION;
    position.motion.windowID = pointer.type == SDL_EVENT_MOUSE_MOTION
                                   ? pointer.motion.windowID
                                   : pointer.button.windowID;
    position.motion.which = SDL_TOUCH_MOUSEID;
    position.motion.x = pointer.type == SDL_EVENT_MOUSE_MOTION
                            ? pointer.motion.x
                            : pointer.button.x;
    position.motion.y = pointer.type == SDL_EVENT_MOUSE_MOTION
                            ? pointer.motion.y
                            : pointer.button.y;
    ImGui_ImplSDL3_ProcessEvent(&position);
    ImGui_ImplSDL3_ProcessEvent(&pointer);
  }
  if (event.type == SDL_EVENT_QUIT ||
      (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
       event.window.windowID == SDL_GetWindowID(window.get())))
    quit();
  return SDL_APP_CONTINUE;
}

SDL_AppResult Launcher::iterate() {
  open();
  if (dialog) {
    std::string path;
    bool finished = false;
    {
      std::lock_guard lock(dialog->mutex);
      finished = dialog->finished;
      path = dialog->path;
    }
    if (finished) {
      dialog.reset();
      auto done = std::move(dialogDone);
      if (!path.empty() && done)
        done(path);
    }
  }
  draw();
  return SDL_APP_CONTINUE;
}

void Launcher::draw() {
  ImGui::SetCurrentContext(context);
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  auto &io = ImGui::GetIO();
  int pixelWidth = 0, pixelHeight = 0;
  if (!SDL_GetCurrentRenderOutputSize(renderer.get(), &pixelWidth,
                                      &pixelHeight))
    throw std::runtime_error(SDL_GetError());
  if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0 || pixelWidth <= 0 ||
      pixelHeight <= 0) {
    ImGui::NewFrame();
    ImGui::EndFrame();
    return;
  }
  io.DisplayFramebufferScale = {pixelWidth / io.DisplaySize.x,
                                pixelHeight / io.DisplaySize.y};
  const float displayScale = SDL_GetWindowDisplayScale(window.get());
  const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
  const float scale =
      displayScale > 0 && pixelDensity > 0 ? displayScale / pixelDensity : 1.0f;
  if (contentScale != scale) {
    ImGuiTheme::apply(scale);
    contentScale = scale;
  }
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(20 * contentScale, 18 * contentScale));
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(3);
  {
    // Header band: accent strip + large title + settings location.
    const ImVec2 origin = ImGui::GetWindowPos();
    const float band = 78 * contentScale;
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilledMultiColor(
        origin, {origin.x + io.DisplaySize.x, origin.y + band},
        IM_COL32(58, 26, 86, 255), IM_COL32(30, 18, 52, 255),
        IM_COL32(23, 18, 33, 255), IM_COL32(23, 18, 33, 255));
    draw->AddRectFilled({origin.x, origin.y + band - 2 * contentScale},
                        {origin.x + io.DisplaySize.x, origin.y + band},
                        ImGui::GetColorU32(ImGuiTheme::accent()));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.7f);
    ImGui::TextColored(ImGuiTheme::accent(), "%s", title.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", settingsPath.empty()
                                  ? "Settings will not be saved"
                                  : settingsPath.c_str());
    ImGui::SetCursorScreenPos(
        {ImGui::GetCursorScreenPos().x,
         std::max(ImGui::GetCursorScreenPos().y, origin.y + band) +
             10 * contentScale});
  }
  const float footer = ImGui::GetFrameHeightWithSpacing() * 2.5f;
  ImGui::BeginChild("##options", {0, -footer});
  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Game")) {
      drawGameOptions();
      ImGuiTheme::sectionHeader("Asset overlay");
      // The text input holds the absolute path. When confirmed it is stored as
      // a (name -> absolute path) pair; the display always shows the absolute
      // path. The persisted absolute is tried first, otherwise the name is
      // resolved through the standard overlay folder search.
      if (ImGui::InputText("Overlay folder", &draft.assetOverlayPath,
                           ImGuiInputTextFlags_EnterReturnsTrue) ||
          !ImGui::IsItemActive())
        selectOverlayFolder(draft.assetOverlayPath);
      if (ImGui::Button("Browse overlay..."))
        browseFolder(draft.assetOverlayPath, [this](const std::string &path) {
          selectOverlayFolder(path);
        });
      ImGui::SameLine();
      if (ImGui::Button("Automatic overlay")) {
        draft.assetOverlayPath.clear();
        draft.assetOverlayName.clear();
        checkOverlay(true);
      }

      checkOverlay();
      ImGui::PushStyleColor(ImGuiCol_Text, overlayValid
                                               ? ImGuiTheme::success()
                                               : ImGuiTheme::warning());
      ImGui::TextWrapped("%s", overlayInfo.c_str());
      ImGui::PopStyleColor();

      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Video")) {
      drawVideo();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Upscaling")) {
      drawUpscaling();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Translation")) {
      drawTranslation();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Advanced")) {
      drawAdvanced();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::EndChild();
  ImGui::Separator();
  if (!status.empty())
    ImGuiTheme::statusText(status.c_str(), statusError);
  else
    ImGui::NewLine();
  if (ImGuiTheme::primaryButton("Play", {120 * contentScale, 0}))
    launch();
  ImGui::SameLine();
  if (ImGui::Button("Restore defaults")) {
    draft = defaults;
    setStatus("Defaults restored (saved on Play)", false);
  }
  ImGui::SameLine();
  if (ImGui::Button("Quit"))
    quit();
  ImGui::End();
  ImGui::Render();
  SDL_SetRenderScale(renderer.get(), io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y);
  SDL_SetRenderDrawColor(renderer.get(), 23, 18, 33, 255);
  SDL_RenderClear(renderer.get());
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
  SDL_RenderPresent(renderer.get());
}

void Launcher::drawVideo() {
  if (canChangeDisplaySize()) {
    ImGuiTheme::sectionHeader("Display");
    int mode = static_cast<int>(draft.mode);
    if (ImGui::Combo("Mode", &mode, Modes, 3))
      draft.mode = static_cast<WindowMode>(mode);

    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (count > 0) {
      draft.display = std::clamp(draft.display, 0, count - 1);
      const char *name = SDL_GetDisplayName(displays[draft.display]);
      const std::string current =
          std::to_string(draft.display + 1) + ": " + (name ? name : "Monitor");
      if (ImGui::BeginCombo("Monitor", current.c_str())) {
        for (int i = 0; i < count; ++i) {
          const char *label = SDL_GetDisplayName(displays[i]);
          const std::string text =
              std::to_string(i + 1) + ": " + (label ? label : "Monitor");
          if (ImGui::Selectable(text.c_str(), i == draft.display))
            draft.display = i;
        }
        ImGui::EndCombo();
      }
    }

    const std::string size =
        std::to_string(draft.width) + " x " + std::to_string(draft.height);
    if (ImGui::BeginCombo("Window", size.c_str())) {
      for (const auto &[w, h] : Resolutions) {
        const std::string text = std::to_string(w) + " x " + std::to_string(h);
        if (ImGui::Selectable(text.c_str(),
                              w == draft.width && h == draft.height)) {
          draft.width = w;
          draft.height = h;
        }
      }
      ImGui::EndCombo();
    }
    int custom[2]{draft.width, draft.height};
    if (ImGui::InputInt2("Custom size", custom)) {
      draft.width = std::clamp(custom[0], 320, 16384);
      draft.height = std::clamp(custom[1], 240, 16384);
    }

    if (draft.mode == WindowMode::Exclusive && count > 0) {
      int modes = 0;
      SDL_DisplayMode **list =
          SDL_GetFullscreenDisplayModes(displays[draft.display], &modes);
      std::string current = "Desktop resolution";
      if (draft.fullscreenWidth > 0)
        current = std::to_string(draft.fullscreenWidth) + " x " +
                  std::to_string(draft.fullscreenHeight) + " @ " +
                  std::to_string(static_cast<int>(draft.refreshRate + 0.5f)) +
                  " Hz";
      if (ImGui::BeginCombo("Exclusive resolution", current.c_str())) {
        if (ImGui::Selectable("Desktop resolution",
                              draft.fullscreenWidth == 0)) {
          draft.fullscreenWidth = draft.fullscreenHeight = 0;
          draft.refreshRate = 0;
        }
        for (int i = 0; list && i < modes; ++i) {
          const SDL_DisplayMode &m = *list[i];
          const std::string text =
              std::to_string(m.w) + " x " + std::to_string(m.h) + " @ " +
              std::to_string(static_cast<int>(m.refresh_rate + 0.5f)) + " Hz";
          const bool selected = m.w == draft.fullscreenWidth &&
                                m.h == draft.fullscreenHeight &&
                                m.refresh_rate == draft.refreshRate;
          if (ImGui::Selectable(text.c_str(), selected)) {
            draft.fullscreenWidth = m.w;
            draft.fullscreenHeight = m.h;
            draft.refreshRate = m.refresh_rate;
          }
        }
        ImGui::EndCombo();
      }
      SDL_free(list);
    }
    if (count > 0) {
      SDL_free(displays);
    }
  } else {
    ImGui::TextDisabled(
        "Display size and monitor settings are managed by Android/iOS.");
  }

  ImGui::Checkbox("VSync", &draft.vsync);
  if (canChangeDisplaySize()) {
    ImGui::SameLine();
    ImGui::Checkbox("Resizable window", &draft.resizable);
  }

  ImGuiTheme::sectionHeader("Renderer");
  const std::string renderer =
      draft.renderer.empty() ? "Automatic" : draft.renderer;
  if (ImGui::BeginCombo("Renderer", renderer.c_str())) {
    if (ImGui::Selectable("Automatic", draft.renderer.empty()))
      draft.renderer.clear();
    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
      const char *driver = SDL_GetRenderDriver(i);
      if (driver && ImGui::Selectable(driver, draft.renderer == driver))
        draft.renderer = driver;
    }
    ImGui::EndCombo();
  }
}

void Launcher::drawUpscaling() {
  auto &upscaling = draft.upscaling;
  ImGuiTheme::sectionHeader("Scale");
  // Same rule as Kotonoha: 1.0 is off, anything above turns it on.
  ImGui::SliderFloat("Scale", &upscaling.factor, 1.0f, 4.0f,
                     upscaling.factor <= 1 ? "Off (1.00x)" : "%.2fx");
  upscaling.factor = std::clamp(upscaling.factor, 1.0f, 4.0f);
  ImGui::TextDisabled("1.0 = off; above 1.0 = on (Ctrl+click "
                      "to type)");
  if (ImGui::Button("Turn off"))
    upscaling.factor = 1;
  for (float preset : {1.5f, 2.0f, 3.0f, 4.0f}) {
    ImGui::SameLine();
    const std::string label = std::to_string(preset).substr(0, 3) + "x";
    if (ImGui::Button(label.c_str()))
      upscaling.factor = preset;
  }

  ImGuiTheme::sectionHeader("Backend");
  if (ImGui::BeginCombo("Backend", backendLabel(upscaling.backend))) {
    for (auto backend : {UpscaleBackend::Auto, UpscaleBackend::Linear,
                         UpscaleBackend::MPV, UpscaleBackend::MetalFX}) {
#ifndef __APPLE__
      if (backend == UpscaleBackend::MetalFX)
        continue;
#endif
      if (ImGui::Selectable(backendLabel(backend),
                            backend == upscaling.backend)) {
        upscaling.backend = backend;
        if (backend == UpscaleBackend::MPV)
          draft.renderer = isMobilePlatform() ? "opengles2" : "opengl";
      }
    }
    ImGui::EndCombo();
  }
  const bool mpv = upscaling.backend == UpscaleBackend::MPV ||
                   (upscaling.backend == UpscaleBackend::Auto &&
                    draft.renderer.rfind("opengl", 0) == 0);
  if (mpv && ImGui::BeginCombo("Shader (OpenGL)",
                               UpscalePresetName(upscaling.preset))) {
    for (auto preset : UpscalePresets())
      if (ImGui::Selectable(UpscalePresetName(preset),
                            preset == upscaling.preset)) {
        upscaling.preset = preset;
        upscaling.shaderFiles.clear();
        upscaling.factor = std::max(2.0f, upscaling.factor);
        if (upscaling.backend == UpscaleBackend::None)
          upscaling.backend = UpscaleBackend::Auto;
      }
    ImGui::EndCombo();
  }
  if (mpv)
    shaderPicker->draw(window.get(), upscaling);
  if (upscaling.backend == UpscaleBackend::None)
    ImGui::TextDisabled("Backend 'None' keeps upscaling off");
  if (const char *required =
          requiredRenderer(upscaling.backend, draft.renderer))
    ImGui::TextColored(ImGuiTheme::warning(),
                       "%s requires the %s renderer; without it the engine "
                       "uses Automatic",
                       backendLabel(upscaling.backend), required);
  ImGui::TextWrapped("Automatic uses MetalFX on the metal renderer, Anime4K/"
                     "ArtCNN on OpenGL and Linear elsewhere.");
}

void Launcher::drawAdvanced() {
  ImGui::SliderFloat("Master volume", &draft.masterVolume, 0, 1, "%.2f");
  ImGui::Checkbox("Mute all audio", &draft.masterMuted);
  for (const auto &name : audioChannels) {
    auto [entry, inserted] = draft.channelVolumes.try_emplace(name, 1.f);
    ImGui::SliderFloat((name + "##mixer").c_str(), &entry->second, 0, 1,
                       "%.2f");
  }
  ImGuiTheme::sectionHeader("VN text");
  const auto vnFonts = VNFontOptions();
  if (ImGui::BeginCombo("VN font", draft.vnFontPath.empty()
                                       ? "Original game font"
                                       : draft.vnFontPath.c_str())) {
    for (const auto &font : vnFonts)
      if (ImGui::Selectable(font.empty() ? "Original game font" : font.c_str(),
                            draft.vnFontPath == font))
        draft.vnFontPath = font;
    ImGui::EndCombo();
  }
  ImGui::Checkbox("Override VN text colors", &draft.vnTextColorOverride);
  auto chooseColor = [&](const char *label, SDL_Color &color) {
    if (ImGui::BeginCombo(label, label)) {
      for (const auto &[name, value] : VNColorOptions)
        if (ImGui::Selectable(name)) {
          color = value;
          draft.vnTextColorOverride = true;
        }
      ImGui::EndCombo();
    }
    float rgba[]{color.r / 255.f, color.g / 255.f, color.b / 255.f,
                 color.a / 255.f};
    if (ImGui::ColorEdit4((std::string(label) + "##custom").c_str(), rgba)) {
      color = {
          Uint8(std::lround(rgba[0] * 255)), Uint8(std::lround(rgba[1] * 255)),
          Uint8(std::lround(rgba[2] * 255)), Uint8(std::lround(rgba[3] * 255))};
      draft.vnTextColorOverride = true;
    }
  };
  chooseColor("Dialogue color", draft.vnTextColor);
  chooseColor("Speaker color", draft.vnSpeakerColor);
  ImGui::SliderFloat("Debug UI scale", &draft.debugUIScale, 1, 3, "%.2fx");
  ImGuiTheme::sectionHeader("UI font");
  drawFontSelector(draft.fontPath);
  ImGuiTheme::sectionHeader("Subtitles");
  ImGui::Checkbox("Enable forced subtitles", &draft.enableForcedSubtitles);
  ImGui::TextWrapped(
      "Load .ass files from \"forced_subtitles\" folder for each node");
  ImGui::Separator();
  int priority = static_cast<int>(draft.logPriority) - SDL_LOG_PRIORITY_TRACE;
  if (ImGui::Combo("Log level", &priority, Priorities, 7))
    draft.logPriority =
        static_cast<SDL_LogPriority>(priority + SDL_LOG_PRIORITY_TRACE);
}

namespace {
struct FontOption {
  std::string filename;
  std::string label;
  bool isCJK = false;
};

std::vector<FontOption> getAvailableFonts() {
  std::vector<FontOption> options;
  try {
    for (const auto &[name, bytes] : FileResolver::getFontMap()) {
      bool cjk = (name.find("JP") != std::string::npos ||
                  name.find("Japanese") != std::string::npos ||
                  name.find("CJK") != std::string::npos ||
                  name.find("Gothic") != std::string::npos ||
                  name.find("Mincho") != std::string::npos ||
                  name.find("NotoSans") != std::string::npos);
      std::string label = name + (cjk ? " [CJK / JP]" : " [Latin]");
      options.push_back({name, label, cjk});
    }
  } catch (...) {
  }
  std::sort(options.begin(), options.end(),
            [](const FontOption &a, const FontOption &b) {
              if (a.isCJK != b.isCJK)
                return a.isCJK > b.isCJK;
              return a.filename < b.filename;
            });
  return options;
}
} // namespace

void Launcher::drawTranslation() {
  ImGui::TextUnformatted("Translation dictionary (JSON)");
  const ImGuiStyle &style = ImGui::GetStyle();
  constexpr const char *Browse = "Browse...";
  constexpr const char *Clear = "Clear";

  const float buttonWidth = ImGui::CalcTextSize(Browse).x +
                            ImGui::CalcTextSize(Clear).x +
                            style.ItemSpacing.x * 2 + style.FramePadding.x * 4;
  ImGui::SetNextItemWidth(-buttonWidth - style.ItemSpacing.x);
  ImGui::InputText("##translationPath", &draft.translationPath);
  ImGui::SameLine();
  if (ImGui::Button("Browse...##translation")) {
    const std::string initial =
        draft.translationPath.empty()
            ? std::string()
            : std::filesystem::path(draft.translationPath)
                  .parent_path()
                  .string();
    browseFile({{"Translation JSON (*.json)", "json"}, {"All files (*)", "*"}},
               initial, [this](const std::string &path) {
                 draft.translationPath = path;
               });
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear##translation")) {
    draft.translationPath.clear();
  }

  std::error_code ec;
  if (!draft.translationPath.empty()) {
    if (std::filesystem::exists(draft.translationPath, ec)) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::success());
      ImGui::TextUnformatted("Translation file found");
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::warning());
      ImGui::TextUnformatted("Translation file not found");
      ImGui::PopStyleColor();
    }
  } else {
    ImGui::TextDisabled("No translation file selected");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::TextDisabled("The default UI font is chosen in Advanced → UI font "
                      "(applies to UI, ImGui and Debug UI).");
}

void Launcher::drawFontSelector(std::string &fontPath) {
  const ImGuiStyle &style = ImGui::GetStyle();
  constexpr const char *Browse = "Browse...";
  constexpr const char *Clear = "Clear";

  const float buttonWidth = ImGui::CalcTextSize(Browse).x +
                            ImGui::CalcTextSize(Clear).x +
                            style.ItemSpacing.x * 2 + style.FramePadding.x * 4;
  ImGui::SetNextItemWidth(-buttonWidth - style.ItemSpacing.x);
  ImGui::InputText("##fontPath", &fontPath);

  auto fonts = getAvailableFonts();

  std::string currentLabel =
      fontPath.empty() ? std::string(defaultFontName()) : fontPath;
  for (const auto &f : fonts) {
    if (f.filename == fontPath) {
      currentLabel = f.label;
      break;
    }
  }

  const bool fontChanged = fontPath != defaults.fontPath;
  ImGui::SetNextItemWidth(-buttonWidth - style.ItemSpacing.x);
  if (ImGui::BeginCombo("##fontCombo", currentLabel.c_str())) {
    for (const auto &f : fonts) {
      bool selected = (f.filename == fontPath);
      if (ImGui::Selectable(f.label.c_str(), selected)) {
        fontPath = f.filename;
        ImGuiTheme::loadFonts(fontPath);
        ImGuiTheme::apply(contentScale);
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear##font")) {
    fontPath = defaults.fontPath;
    ImGuiTheme::loadFonts(fontPath);
    ImGuiTheme::apply(contentScale);
  }

  if (!fontPath.empty()) {
    std::filesystem::path resolvedFont;
    try {
      resolvedFont = FileResolver::font(fontPath);
    } catch (const std::exception &) {
    }
    if (!resolvedFont.empty() && std::filesystem::exists(resolvedFont)) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::success());
      ImGui::TextUnformatted("UI font file found");
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::warning());
      ImGui::TextUnformatted("UI font file not found");
      ImGui::PopStyleColor();
    }
  } else {
    ImGui::TextDisabled(
        "Default engine font (Noto Sans JP / CJK & Latin) will be used");
  }
}

} // namespace Kotonoha
