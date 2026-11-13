#include <Kotonoha/components/DisplayModeCatalog.hpp>
#include <Kotonoha/components/SettingsMenu.hpp>
#include <Kotonoha/core/Appearance.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/debug/ImGuiTheme.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace Kotonoha {
SettingsMenu::SettingsMenu(Engine &engine)
    : engine(engine),
      shaderPicker(std::make_unique<ImGuiTheme::ShaderFilePicker>()) {}
SettingsMenu::~SettingsMenu() = default;

int SettingsMenu::pageCount() const { return engine.GamepadCount() ? 4 : 3; }

std::vector<int> SettingsMenu::rows() const {
  std::vector<int> result;
  if (page == 0) {
    if (canChangeDisplaySize())
      result.insert(result.end(), {0, 1, 2, 3});
    result.insert(result.end(), {4, 5});
    if (engine.GetUpscaler().resolve(draft.upscaling.backend) ==
        UpscaleBackend::MPV)
      result.insert(result.end(), {6, 10});
    result.push_back(7);
  } else if (page == 1) {
    result.insert(result.end(), {11, 12});
    for (int i = 0; i < int(channels.size()); ++i)
      result.push_back(100 + i);
  } else if (page == 2)
    result.insert(result.end(), {18, 13, 14, 15, 16});
  else
    result.push_back(17);
  result.insert(result.end(), {8, 9});
  return result;
}

void SettingsMenu::open() {
  touchActivation.reset();
  draft = engine.GetSettings();
  selected = first = page = 0;
  slider = -1;
  scrolling.reset();
  channels.clear();
  // Lista apenas os canais de áudio reais; não cria canais novos apenas para
  // exibição no mixer.
  for (const auto &channel : engine.GetAudio().getChannels()) {
    channels.push_back(channel->getName());
    draft.channelVolumes.try_emplace(channel->getName(),
                                     channel->getUserVolume());
  }
  for (const auto &[name, volume] : draft.channelVolumes)
    if (std::find(channels.begin(), channels.end(), name) == channels.end())
      channels.push_back(name);
  fonts = VNFontOptions();
  error.clear();
  visible = true;
}

void SettingsMenu::applyAudioDraft() {
  auto next = engine.GetSettings();
  next.masterVolume = draft.masterVolume;
  next.masterMuted = draft.masterMuted;
  next.channelVolumes = draft.channelVolumes;
  try {
    // Audio is applied live: no apply/save step is required for the mixer.
    engine.applySettings(next, false);
  } catch (const std::exception &failure) {
    error = failure.what();
    SDL_LogError(LogConfig, "Settings audio: %s", error.c_str());
  }
}

void SettingsMenu::applyTextSpeedDraft() {
  auto next = engine.GetSettings();
  next.vnTextSpeed = draft.vnTextSpeed;
  next.vnTextInstant = draft.vnTextInstant;
  try {
    engine.applySettings(next, false);
  } catch (const std::exception &failure) {
    error = failure.what();
    SDL_LogError(LogConfig, "Settings text speed: %s", error.c_str());
  }
}

void SettingsMenu::persistAudio() {
  try {
    engine.saveSettings();
  } catch (const std::exception &failure) {
    error = failure.what();
    SDL_LogError(LogConfig, "Settings save: %s", error.c_str());
  }
}

void SettingsMenu::close() {
  persistAudio();
  visible = false;
  selectBox.close();
  slider = -1;
}

void SettingsMenu::activate(int index, int direction) {
  error.clear();
  if (index == 0)
    draft.mode =
        static_cast<WindowMode>((static_cast<int>(draft.mode) + 1) % 3);
  else if (index == 1) {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (!displays || count < 1) {
      if (displays)
        SDL_free(displays);
      throw std::runtime_error("No displays available");
    }
    const SDL_DisplayID display =
        displays[std::clamp(draft.display, 0, count - 1)];
    SDL_free(displays);
    const auto available = DisplayModeCatalog::Enumerate(display);
    if (available.empty())
      throw std::runtime_error("No supported resolutions for this monitor");
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
    const bool nativeBorderless =
        draft.mode == WindowMode::Borderless && desktop;
    const int width = nativeBorderless ? desktop->w
                                       : (draft.mode == WindowMode::Windowed ||
                                                  draft.fullscreenWidth <= 0
                                              ? draft.width
                                              : draft.fullscreenWidth);
    const int height = nativeBorderless ? desktop->h
                                        : (draft.mode == WindowMode::Windowed ||
                                                   draft.fullscreenHeight <= 0
                                               ? draft.height
                                               : draft.fullscreenHeight);
    const float refreshRate =
        nativeBorderless ? desktop->refresh_rate : draft.refreshRate;
    const int current =
        DisplayModeCatalog::FindSelected(available, width, height, refreshRate);
    std::vector<std::string> labels;
    labels.reserve(available.size());
    for (const auto &mode : available)
      labels.push_back(DisplayModeCatalog::Label(mode));
    // The shared selector supports wheel, touch, controller/keyboard and a
    // scroll thumb; do not truncate the driver's list to a fixed preset array.
    selectBox.open(
        "Monitor resolutions / refresh rates", std::move(labels), current,
        [this, available,
         desktopMode = desktop ? std::optional<SDL_DisplayMode>(*desktop)
                               : std::nullopt](int choice) {
          DisplayModeCatalog::Select(draft, available.at(choice),
                                     desktopMode ? &*desktopMode : nullptr);
        });
  } else if (index == 2) {
    int count = 0;
    auto *displays = SDL_GetDisplays(&count);
    if (!displays)
      throw std::runtime_error(SDL_GetError());
    if (count < 1) {
      SDL_free(displays);
      throw std::runtime_error("No displays available");
    }
    draft.display = (draft.display + 1) % count;
    if (const SDL_DisplayMode *desktop =
            SDL_GetDesktopDisplayMode(displays[draft.display])) {
      // Fullscreen modes are monitor-specific; don't carry an unsupported
      // exclusive tuple to the next monitor when the player switches it.
      draft.fullscreenWidth = desktop->w;
      draft.fullscreenHeight = desktop->h;
      draft.refreshRate = desktop->refresh_rate;
    }
    SDL_free(displays);
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
  } else if (index == 6) {
    std::vector<std::string> names;
    int current = 0;
    const auto &presets = UpscalePresets();
    for (int i = 0; i < int(presets.size()); ++i) {
      names.push_back(UpscalePresetName(presets[i]));
      if (presets[i] == draft.upscaling.preset)
        current = i;
    }
    selectBox.open("OpenGL shader", names, current, [this](int choice) {
      draft.upscaling.preset = UpscalePresets()[choice];
      draft.upscaling.shaderFiles.clear();
      draft.upscaling.factor = std::max(2.f, draft.upscaling.factor);
    });
  } else if (index == 7)
    draft.upscaling.factor =
        draft.upscaling.factor >= 4 ? 1 : draft.upscaling.factor + 0.5f;
  else if (index == 8) {
    // On the Audio page everything is already live; just close the menu.
    if (page != 1)
      engine.applySettings(draft);
    close();
  } else if (index == 11) {
    draft.masterVolume = std::clamp(
        std::round((draft.masterVolume + direction * 0.05f) * 100) / 100, 0.0f,
        1.0f);
    applyAudioDraft();
  } else if (index == 12) {
    draft.masterMuted = !draft.masterMuted;
    applyAudioDraft();
  } else if (index == 10)
    shaderPicker->browse(&engine.GetWindow());
  else if (index == 13) {
    auto labels = fonts;
    labels.front() = "Original game font";
    auto found = std::find(fonts.begin(), fonts.end(), draft.vnFontPath);
    selectBox.open("VN font", labels,
                   int(found == fonts.end() ? 0 : found - fonts.begin()),
                   [this](int i) { draft.vnFontPath = fonts[i]; });
  } else if (index == 14)
    draft.vnTextColorOverride = !draft.vnTextColorOverride;
  else if (index == 15 || index == 16) {
    std::vector<std::string> labels;
    for (const auto &[name, color] : VNColorOptions)
      labels.push_back(name);
    selectBox.open(index == 15 ? "Dialogue color" : "Speaker color", labels, 0,
                   [this, index](int i) {
                     (index == 15 ? draft.vnTextColor : draft.vnSpeakerColor) =
                         VNColorOptions[i].second;
                     draft.vnTextColorOverride = true;
                   });
  } else if (index == 17)
    draft.swapControllerAB = !draft.swapControllerAB;
  else if (index == 18) {
    const std::vector<float> rates{0, 10, 20, 30, 45, 60, 90, 120};
    std::vector<std::string> labels{"Original game speed"};
    int current = 0;
    for (size_t i = 1; i < rates.size(); ++i) {
      labels.push_back(std::to_string(int(rates[i])) + " characters / second");
      if (draft.vnTextSpeed == rates[i])
        current = int(i);
    }
    labels.push_back("Instant");
    if (draft.vnTextInstant)
      current = int(rates.size());
    selectBox.open("Typewriter speed", labels, current,
                   [this, rates](int choice) {
                     draft.vnTextInstant = choice == int(rates.size());
                     draft.vnTextSpeed =
                         draft.vnTextInstant ? 0 : rates.at(size_t(choice));
                     applyTextSpeedDraft();
                   });
  } else if (index >= 100) {
    auto &volume = draft.channelVolumes[channels.at(index - 100)];
    volume = std::clamp(volume + direction * .05f, 0.f, 1.f);
    applyAudioDraft();
  } else
    close();
}

bool SettingsMenu::onEvent(EventRef rawEvent) {
  if (!visible)
    return false;
  page = std::min(page, pageCount() - 1);
  const auto event = MapControllerEvent(rawEvent);
  if (!touchActivation.accept(event)) {
    slider = -1;
    return true;
  }
  if (selectBox.isOpen()) {
    if (BackAction(engine, event)) {
      selectBox.close();
      return true;
    } else {
      selectBox.onEvent(event);
      return true;
    }
  }
  if (BackAction(engine, event)) {
    close();
    return true;
  }
  if (rawEvent.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && !selectBox.isOpen() &&
      (rawEvent.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ||
       rawEvent.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
    page = (page + (rawEvent.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER
                        ? pageCount() - 1
                        : 1)) %
           pageCount();
    selected = first = 0;
    return true;
  }
  auto items = rows();
  auto setSlider = [&](int id, float x) {
    float value = std::clamp((x - 660) / 380, 0.f, 1.f);
    if (id == 11)
      draft.masterVolume = value;
    else
      draft.channelVolumes[channels.at(id - 100)] = value;
    applyAudioDraft();
  };
  if (slider >= 0) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      setSlider(slider, event.motion.x);
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      slider = -1;
      persistAudio();
      return false;
    }
  }
  const int tabs = pageCount();
  const float tabWidth = tabs == 4 ? 205.f : 280.f;
  const float tabStep = tabs == 4 ? 220.f : 290.f;
  for (int p = 0; p < tabs; ++p)
    if (UI::clicked(event, {200.f + p * tabStep, 85, tabWidth, 48})) {
      page = p;
      selected = first = 0;
      return true;
    }
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_TAB) {
      page = (page + 1) % tabs;
      selected = first = 0;
      return true;
    }
    const bool navigation =
        event.key.key == SDLK_END || event.key.key == SDLK_HOME ||
        event.key.key == SDLK_DOWN || event.key.key == SDLK_UP;
    if (event.key.key == SDLK_END)
      selected = int(items.size()) - 2;
    else if (event.key.key == SDLK_HOME)
      selected = 0;
    else if (event.key.key == SDLK_DOWN)
      selected = (selected + 1) % int(items.size());
    else if (event.key.key == SDLK_UP)
      selected = (selected + int(items.size()) - 1) % int(items.size());
    if (navigation) {
      if (selected < int(items.size()) - 2)
        first = std::clamp(first, std::max(0, selected - 8), selected);
      return true;
    }
  }
  if (slider < 0 && event.type >= SDL_EVENT_FINGER_DOWN &&
      event.type <= SDL_EVENT_FINGER_CANCELED) {
    scrollOffset = first * 48.f;
    if (scrolling.handle(event, 720, scrollOffset, scrollVelocity, scrollFollow,
                         std::max(0, int(items.size()) - 11) * 48.f)) {
      first = int(scrollOffset / 48);
      return true;
    }
  }
  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    first = std::clamp(first - int(event.wheel.y), 0,
                       std::max(0, int(items.size()) - 11));
    return true;
  }
  try {
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE ||
         event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
      selected = std::clamp(selected, 0, int(items.size()) - 1);
      activate(items[selected], event.key.key == SDLK_LEFT ? -1 : 1);
      return true;
    }
    if (UI::clicked(event, {200, 624, 420, 44})) {
      activate(8);
      return true;
    }
    if (UI::clicked(event, {640, 624, 420, 44})) {
      close();
      return true;
    }
    for (int i = first; i < std::min(first + 9, int(items.size()) - 2); ++i) {
      const float y = 150 + (i - first) * 48;
      const int id = items[i];
      if ((id == 11 || id >= 100) &&
          event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          event.button.button == SDL_BUTTON_LEFT) {
        SDL_FPoint point{event.button.x, event.button.y};
        SDL_FRect bar{650, y, 410, 44};
        if (SDL_PointInRectFloat(&point, &bar)) {
          selected = i;
          slider = id;
          setSlider(id, point.x);
          return true;
        }
      }
      if (UI::clicked(event, {200, y, 860, 44})) {
        selected = i;
        activate(id);
        return true;
      }
    }
  } catch (const std::exception &failure) {
    error = failure.what();
    SDL_LogError(LogConfig, "Settings: %s", error.c_str());
  }
  return false;
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
  constexpr const char *backends[]{"Off", "Linear (SDL)", "MPV / OpenGL",
                                   "MetalFX", "Automatic"};
  DisplayModeChoice shown{draft.width, draft.height, draft.refreshRate};
  if (draft.mode != WindowMode::Windowed && draft.fullscreenWidth > 0 &&
      draft.fullscreenHeight > 0) {
    shown.width = draft.fullscreenWidth;
    shown.height = draft.fullscreenHeight;
  }
  if (draft.mode == WindowMode::Borderless) {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (displays && draft.display >= 0 && draft.display < count) {
      if (const auto *desktop =
              SDL_GetDesktopDisplayMode(displays[draft.display]))
        shown = {desktop->w, desktop->h, desktop->refresh_rate};
    }
    if (displays)
      SDL_free(displays);
  }
  const std::string resolutionLabel =
      shown.refreshRate > 0.0f
          ? DisplayModeCatalog::Label(shown)
          : std::to_string(shown.width) + " x " + std::to_string(shown.height);
  const std::vector<std::string> labels{
      std::string("Mode: ") + modes[int(draft.mode)],
      "Resolution: " + resolutionLabel + "  (select...) ",
      "Monitor: " + std::to_string(draft.display + 1),
      std::string("Resizable: ") + (draft.resizable ? "Yes" : "No"),
      std::string("VSync: ") + (draft.vsync ? "Yes" : "No"),
      std::string("Upscaler: ") + backends[int(draft.upscaling.backend)],
      std::string("Shader: ") + UpscalePresetName(draft.upscaling.preset),
      "Factor: " + std::to_string(draft.upscaling.factor).substr(0, 3) + "x",
      "Apply and save",
      "Cancel",
      "Custom GLSL files...",
      "Master volume",
      std::string("Mute all: ") + (draft.masterMuted ? "Yes" : "No"),
      std::string("VN font: ") +
          (draft.vnFontPath.empty() ? "Original game font" : draft.vnFontPath),
      std::string("Override text colors: ") +
          (draft.vnTextColorOverride ? "Yes" : "No"),
      "Dialogue color...",
      "Speaker color...",
      std::string("Controller A/B: ") + (draft.swapControllerAB
                                             ? "Swapped (South confirms)"
                                             : "Standard (East confirms)"),
      std::string("Typewriter speed: ") +
          (draft.vnTextInstant      ? "Instant"
           : draft.vnTextSpeed == 0 ? "Original game speed"
                                    : std::to_string(int(draft.vnTextSpeed)) +
                                          " characters / second")};
  ui.backdrop(renderer, {0, 0, 0, 220});
  ui.panel(renderer, {170, 25, 920, 670});
  ui.text(renderer, "Settings", 200, 36, 860, {255, 215, 100, 255});
  if (page == 1)
    ui.text(renderer, "Audio applies immediately", 780, 44, 280,
            {150, 165, 190, 255});
  if (page == 2)
    ui.text(renderer, "Speed applies immediately", 760, 44, 300,
            {150, 165, 190, 255});
  const int tabs = pageCount();
  const float tabWidth = tabs == 4 ? 205.f : 280.f;
  const float tabStep = tabs == 4 ? 220.f : 290.f;
  page = std::min(page, tabs - 1);
  const char *pages[]{"Display", "Audio", "VN text", "Input"};
  for (int p = 0; p < tabs; ++p)
    ui.button(renderer, {200.f + p * tabStep, 85, tabWidth, 48}, pages[p],
              page == p);
  auto items = rows();
  selected = std::clamp(selected, 0, int(items.size()) - 1);
  first = std::clamp(first, 0, std::max(0, int(items.size()) - 11));
  for (int i = first; i < std::min(first + 9, int(items.size()) - 2); ++i) {
    const int id = items[i];
    const float y = 150 + (i - first) * 48;
    const std::string label = id >= 100 ? channels.at(id - 100) : labels.at(id);
    if (id == 11 || id >= 100) {
      const float value = id == 11
                              ? draft.masterVolume
                              : draft.channelVolumes[channels.at(id - 100)];
      ui.text(renderer,
              label + " " + std::to_string(int(std::lround(value * 100))) + "%",
              210, y + 12, 430);
      ui.panel(renderer, {660, y + 14, 380, 16}, {70, 80, 100, 255});
      ui.panel(renderer, {660, y + 14, 380 * value, 16}, {100, 190, 255, 255});
      ui.panel(renderer, {654 + 380 * value, y + 4, 14, 36},
               {240, 240, 255, 255});
    } else
      ui.button(renderer, {200, y, 860, 44}, label, selected == i);
  }
  ui.button(renderer, {200, 624, 420, 44},
            page == 1 ? "Close" : "Apply and save",
            selected == int(items.size()) - 2);
  ui.button(renderer, {640, 624, 420, 44}, "Cancel",
            selected == int(items.size()) - 1);
  if (!error.empty())
    ui.text(renderer, error, 200, 586, 860, {255, 120, 120, 255});
  selectBox.render(renderer, ui);
}
} // namespace Kotonoha
