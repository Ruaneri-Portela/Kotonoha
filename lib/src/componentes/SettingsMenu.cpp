#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/UI.hpp>
#include "../debug/ImGuiTheme.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {
SettingsMenu::SettingsMenu(Engine &engine)
    : engine(engine), shaderPicker(std::make_unique<ImGuiTheme::ShaderFilePicker>()) {}
SettingsMenu::~SettingsMenu() = default;

std::vector<int> SettingsMenu::rows() const {
  std::vector<int> result{0, 1, 2, 3, 4, 5};
  if (engine.GetUpscaler().resolve(draft.upscaling.backend) == UpscaleBackend::MPV) {
    result.push_back(6);
    result.push_back(10);
  }
  result.insert(result.end(), {7, 11, 12, 8, 9});
  return result;
}

void SettingsMenu::open() {
  draft = engine.GetSettings();
  selected = 0;
  error.clear();
  visible = true;
}

void SettingsMenu::activate(int index, int direction) {
  error.clear();
  if (index == 0)
    draft.mode =
        static_cast<WindowMode>((static_cast<int>(draft.mode) + 1) % 3);
  else if (index == 1) {
    constexpr int sizes[][2]{
        {960, 540}, {1280, 720}, {1600, 900}, {1920, 1080}};
    int next = 0;
    for (int i = 0; i < 4; ++i)
      if (sizes[i][0] == draft.width && sizes[i][1] == draft.height)
        next = (i + 1) % 4;
    draft.width = sizes[next][0];
    draft.height = sizes[next][1];
    draft.fullscreenWidth = draft.width;
    draft.fullscreenHeight = draft.height;
  } else if (index == 2) {
    int count = 0;
    auto *displays = SDL_GetDisplays(&count);
    if (!displays)
      throw std::runtime_error(SDL_GetError());
    SDL_free(displays);
    if (count < 1)
      throw std::runtime_error("No displays available");
    draft.display = (draft.display + 1) % count;
  } else if (index == 3)
    draft.resizable = !draft.resizable;
  else if (index == 4)
    draft.vsync = !draft.vsync;
  else if (index == 5) {
    for (int i = 1; i <= 5; ++i) {
      auto next = static_cast<UpscaleBackend>(
          (static_cast<int>(draft.upscaling.backend) + i) % 5);
      if (engine.GetUpscaler().supports(next)) {
        draft.upscaling.backend = next;
        break;
      }
    }
  } else if (index == 6)
    draft.upscaling.preset = static_cast<UpscalePreset>(
        (static_cast<int>(draft.upscaling.preset) + 1) % 4);
  else if (index == 7)
    draft.upscaling.factor =
        draft.upscaling.factor >= 4 ? 1 : draft.upscaling.factor + 0.5f;
  else if (index == 8) {
    engine.applySettings(draft);
    close();
  } else if (index == 11)
    draft.masterVolume = std::clamp(
        std::round((draft.masterVolume + direction * 0.05f) * 100) / 100, 0.0f, 1.0f);
  else if (index == 12)
    draft.masterMuted = !draft.masterMuted;
  else if (index == 10)
    shaderPicker->browse(&engine.GetWindow());
  else
    close();
}

void SettingsMenu::onEvent(EventRef event) {
  if (!visible)
    return;
  const auto items = rows();
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_ESCAPE) {
      close();
      return;
    }
    if (event.key.key == SDLK_DOWN)
      selected = (selected + 1) % static_cast<int>(items.size());
    if (event.key.key == SDLK_UP)
      selected = (selected + static_cast<int>(items.size()) - 1) % static_cast<int>(items.size());
  }
  try {
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE ||
         event.key.key == SDLK_RIGHT || event.key.key == SDLK_LEFT)) {
      if (event.key.key != SDLK_LEFT || items[selected] == 11)
        activate(items[selected], event.key.key == SDLK_LEFT ? -1 : 1);
      return;
    }
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
      if (UI::clicked(event, {360, 100.0f + i * 38, 560, 34})) {
        selected = i;
        activate(items[i]);
        return;
      }
  } catch (const std::exception &failure) {
    error = failure.what();
    SDL_LogError(LogConfig, "Settings: %s", error.c_str());
  }
}

void SettingsMenu::render(RendererRef renderer) {
  if (!visible)
    return;
  shaderPicker->poll(draft.upscaling);
  if (!shaderPicker->errorMessage().empty())
    error = shaderPicker->errorMessage();
  auto &ui = engine.GetUI();
  constexpr const char *modes[]{"Windowed", "Borderless fullscreen",
                                "Exclusive fullscreen"};
  constexpr const char *backends[]{"Off", "Linear (SDL)",
                                   "Anime4K / ArtCNN (OpenGL)", "MetalFX",
                                   "Automatic"};
  constexpr const char *presets[]{"Anime4K B-S", "ArtCNN C4F16",
                                  "ArtCNN C4F16 DS", "ArtCNN C4F32"};
  const std::array<std::string, 13> labels{
      std::string("Mode: ") + modes[static_cast<int>(draft.mode)],
      "Resolution: " + std::to_string(draft.width) + " x " +
          std::to_string(draft.height),
      "Monitor: " + std::to_string(draft.display + 1),
      std::string("Resizable: ") + (draft.resizable ? "Yes" : "No"),
      std::string("VSync: ") + (draft.vsync ? "Yes" : "No"),
      std::string("Upscaler: ") +
          backends[static_cast<int>(draft.upscaling.backend)],
      std::string("Preset OpenGL: ") +
          presets[static_cast<int>(draft.upscaling.preset)],
      "Factor: " + std::to_string(draft.upscaling.factor).substr(0, 3) + "x" +
          (draft.upscaling.factor <= 1 ? " (off)" : ""),
      "Apply and save",
      "Back without applying",
      "MPV shader files: " + std::to_string(UpscaleShaderFiles(draft.upscaling.shaderFiles).size()) + " (choose...)",
      "Master volume: " + std::to_string(int(std::lround(draft.masterVolume * 100))) + "% (Left / Right)",
      std::string("Mute all audio: ") + (draft.masterMuted ? "Yes" : "No")};
  ui.panel(renderer, {0, 0, UI::width, UI::height}, {0, 0, 0, 210});
  ui.panel(renderer, {330, 30, 620, 650});
  ui.text(renderer, "Settings", 360, 50, 560, {255, 215, 100, 255});
  const auto items = rows();
  for (int i = 0; i < static_cast<int>(items.size()); ++i)
    ui.button(renderer, {360, 100.0f + i * 38, 560, 34}, labels[items[i]],
              selected == i);
  if (!error.empty())
    ui.text(renderer, error, 360, 590, 560, {255, 120, 120, 255});
}
} // namespace Kotonoha
