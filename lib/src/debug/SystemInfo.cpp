#include <Kotonoha/debug/SystemInfo.hpp>
#include <Kotonoha/debug/ImGuiTheme.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/BuildVersion.hpp>

#include <imgui.h>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

namespace Kotonoha {
namespace {
std::string valid(const char *value) {
  return value && *value ? value : "Unavailable";
}
std::string sizeString(int w, int h) {
  return std::to_string(w) + " x " + std::to_string(h);
}
std::string versionString(int version) {
  return std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." +
         std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
         std::to_string(SDL_VERSIONNUM_MICRO(version));
}
const char *batteryLabel(SDL_PowerState state) {
  switch (state) {
    case SDL_POWERSTATE_ON_BATTERY: return "On battery";
    case SDL_POWERSTATE_NO_BATTERY: return "No battery";
    case SDL_POWERSTATE_CHARGING: return "Charging";
    case SDL_POWERSTATE_CHARGED: return "Charged";
    case SDL_POWERSTATE_UNKNOWN: return "Unknown";
    default: return "Unavailable";
  }
}
void row(SystemInfoSection &section, const char *name, std::string value) {
  section.fields.emplace_back(name, std::move(value));
}
} // namespace

std::string SystemInfoSnapshot::text() const {
  std::ostringstream stream;
  for (const auto &section : sections) {
    stream << "[" << section.title << "]\n";
    for (const auto &[name, value] : section.fields)
      stream << name << ": " << value << '\n';
    stream << '\n';
  }
  return stream.str();
}

SystemInfoSnapshot captureSystemInfo(SDL_Window *window,
                                    SDL_Renderer *renderer) {
  SystemInfoSnapshot result;
  auto &build = result.sections.emplace_back();
  build.title = "Kotonoha build";
  row(build, "Version", BuildVersion::Full);
  row(build, "Base version", BuildVersion::Base);
  row(build, "Git branch", BuildVersion::Branch);
  row(build, "Git commit", BuildVersion::Commit);
  row(build, "Working tree at configure",
      BuildVersion::Dirty ? "Modified" : "Clean / unknown");
  row(build, "Platform", valid(SDL_GetPlatform()));
  row(build, "SDL compiled", versionString(SDL_VERSION));
  row(build, "SDL linked", versionString(SDL_GetVersion()));
  row(build, "SDL revision", valid(SDL_GetRevision()));

  auto &cpu = result.sections.emplace_back();
  cpu.title = "CPU and memory";
  const int cores = SDL_GetNumLogicalCPUCores();
  row(cpu, "Logical CPU cores", cores > 0 ? std::to_string(cores) : "Unknown");
  const int ram = SDL_GetSystemRAM();
  row(cpu, "System RAM (total)", ram > 0 ?
      std::to_string(ram) + " MiB" : "Unavailable");
  const int cacheLine = SDL_GetCPUCacheLineSize();
  row(cpu, "CPU cache line", cacheLine > 0 ?
      std::to_string(cacheLine) + " bytes" : "Unavailable");
  row(cpu, "SIMD alignment",
      std::to_string(SDL_GetSIMDAlignment()) + " bytes");
  std::string simd;
  const auto add = [&](const char *name, bool supported) {
    if (supported) {
      if (!simd.empty()) simd += ", ";
      simd += name;
    }
  };
  add("SSE", SDL_HasSSE());
  add("SSE2", SDL_HasSSE2());
  add("SSE3", SDL_HasSSE3());
  add("SSE4.1", SDL_HasSSE41());
  add("SSE4.2", SDL_HasSSE42());
  add("AVX", SDL_HasAVX());
  add("AVX2", SDL_HasAVX2());
  add("AVX-512F", SDL_HasAVX512F());
  add("NEON", SDL_HasNEON());
  add("ARM SIMD", SDL_HasARMSIMD());
  row(cpu, "SIMD features", simd.empty() ? "None reported" : simd);
  row(cpu, "Free RAM / process usage",
      "Not exposed by SDL3 CPU APIs");

  auto &video = result.sections.emplace_back();
  video.title = "Video and GPU";
  row(video, "SDL video backend", valid(SDL_GetCurrentVideoDriver()));
  row(video, "Renderer", renderer ?
      valid(SDL_GetRendererName(renderer)) : "No renderer");
  const SDL_PropertiesID properties = renderer ?
      SDL_GetRendererProperties(renderer) : 0;
  if (properties) {
    const auto maxTexture = SDL_GetNumberProperty(properties,
        SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
    if (maxTexture > 0)
      row(video, "Max texture dimension",
          std::to_string(maxTexture) + " px");
    const auto vsync = SDL_GetNumberProperty(properties,
        SDL_PROP_RENDERER_VSYNC_NUMBER, -1);
    row(video, "Renderer VSync",
        vsync == -1 ? "Unavailable" : std::to_string(vsync));
  }
  std::string gpuName =
      "Unavailable for this SDL_Renderer backend";
#if defined(SDL_PROP_RENDERER_GPU_DEVICE_POINTER) && defined(SDL_PROP_GPU_DEVICE_NAME_STRING)
  if (properties) {
    auto *device = static_cast<SDL_GPUDevice *>(
        SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_GPU_DEVICE_POINTER,
                               nullptr));
    if (device) {
      const auto gpuProps = SDL_GetGPUDeviceProperties(device);
      if (gpuProps)
        gpuName = valid(SDL_GetStringProperty(gpuProps,
                               SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr));
    }
  }
#endif
  row(video, "GPU hardware", gpuName);
  row(video, "GPU VRAM", "Not exposed by the SDL_Renderer API");
  if (renderer) {
    int w = 0, h = 0;
    if (SDL_GetCurrentRenderOutputSize(renderer, &w, &h))
      row(video, "Render output", sizeString(w, h));
  }
  if (window) {
    row(video, "Window title", valid(SDL_GetWindowTitle(window)));
    row(video, "Window flags", std::to_string(SDL_GetWindowFlags(window)));
    int w = 0, h = 0;
    if (SDL_GetWindowSize(window, &w, &h))
      row(video, "Window logical size", sizeString(w, h));
    if (SDL_GetWindowSizeInPixels(window, &w, &h))
      row(video, "Window pixel size", sizeString(w, h));
    row(video, "Display scale",
        std::to_string(SDL_GetWindowDisplayScale(window)));
    row(video, "Pixel density",
        std::to_string(SDL_GetWindowPixelDensity(window)));
  }
  int displays = 0;
  SDLArrayPtr<SDL_DisplayID> displayIds(SDL_GetDisplays(&displays));
  row(video, "Connected displays", std::to_string(displays));
  for (int i = 0; displayIds && i < displays; ++i) {
    auto *mode = SDL_GetCurrentDisplayMode(displayIds.get()[i]);
    std::string value = valid(SDL_GetDisplayName(displayIds.get()[i]));
    if (mode) {
      value += " - " + sizeString(mode->w, mode->h);
      if (mode->refresh_rate > 0)
        value += " @ " + std::to_string(mode->refresh_rate) + " Hz";
    }
    row(video, ("Display " + std::to_string(i + 1)).c_str(), value);
  }


  auto &power = result.sections.emplace_back();
  power.title = "Power";
  int seconds = -1, percent = -1;
  const auto powerState = SDL_GetPowerInfo(&seconds, &percent);
  row(power, "Status", batteryLabel(powerState));
  row(power, "Battery", percent < 0 ?
      "Unavailable" : std::to_string(percent) + "%");
  row(power, "Remaining time", seconds < 0 ?
      "Unavailable" : std::to_string(seconds / 60) + " min");

  auto &input = result.sections.emplace_back();
  input.title = "Input";
  int pads = 0;
  SDLArrayPtr<SDL_JoystickID> ids(SDL_GetGamepads(&pads));
  row(input, "Connected gamepads", std::to_string(pads));
  for (int i = 0; ids && i < pads; ++i)
    row(input, ("Gamepad " + std::to_string(i + 1)).c_str(),
        valid(SDL_GetGamepadNameForID(ids.get()[i])));

  int touches = 0;
  SDLArrayPtr<SDL_TouchID> touchIds(SDL_GetTouchDevices(&touches));
  row(input, "Touch devices", std::to_string(touches));


  auto &audio = result.sections.emplace_back();
  audio.title = "Audio";
  int count = 0;
  SDLArrayPtr<SDL_AudioDeviceID> audioIds(SDL_GetAudioPlaybackDevices(&count));
  row(audio, "Playback devices", std::to_string(count));
  for (int i = 0; audioIds && i < count; ++i)
    row(audio, ("Output " + std::to_string(i + 1)).c_str(),
        valid(SDL_GetAudioDeviceName(audioIds.get()[i])));

  return result;
}

void drawSystemInfoPanel(SDL_Window *window, SDL_Renderer *renderer) {
  struct Cached {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    bool valid = false;
    SystemInfoSnapshot data;
  };
  static Cached cache;
  if (!cache.valid || cache.window != window || cache.renderer != renderer) {
    cache.data = captureSystemInfo(window, renderer);
    cache.valid = true;
    cache.window = window;
    cache.renderer = renderer;
  }
  ImGui::TextDisabled("SDL3 runtime diagnostics  |  Read-only");
  if (ImGui::Button("Refresh information"))
    cache.data = captureSystemInfo(window, renderer);
  ImGui::SameLine();
  if (ImGui::Button("Copy diagnostic report")) {
    const auto report = cache.data.text();
    ImGui::SetClipboardText(report.c_str());
  }
  ImGui::Spacing();
  for (const auto &section : cache.data.sections) {
    ImGuiTheme::sectionHeader(section.title.c_str());
    if (ImGui::BeginTable(section.title.c_str(), 2,
           ImGuiTableFlags_SizingStretchProp |
               ImGuiTableFlags_BordersInnerH |
               ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.42f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
      for (const auto &[key, value] : section.fields) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", key.c_str());
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", value.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
  }
}
} // namespace Kotonoha
