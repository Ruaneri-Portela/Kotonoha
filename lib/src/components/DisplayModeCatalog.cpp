#include <Kotonoha/components/DisplayModeCatalog.hpp>

#include <algorithm>
#include <cmath>

#if defined(_WIN32) && !defined(__ANDROID__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Kotonoha {
namespace {

constexpr float kRefreshEquality = 0.02f;

bool Valid(DisplayModeChoice mode) {
  return mode.width >= 320 && mode.height >= 240 && mode.width <= 16384 &&
         mode.height <= 16384 && std::isfinite(mode.refreshRate) &&
         mode.refreshRate > 0.0f && mode.refreshRate <= 1000.0f;
}

} // namespace

bool DisplayModeCatalog::AddUnique(std::vector<DisplayModeChoice> &modes,
                                   DisplayModeChoice mode) {
  if (!Valid(mode))
    return false;
  const auto duplicate = std::find_if(
      modes.begin(), modes.end(), [&](const DisplayModeChoice &candidate) {
        return candidate.width == mode.width &&
               candidate.height == mode.height &&
               std::abs(candidate.refreshRate - mode.refreshRate) <
                   kRefreshEquality;
      });
  if (duplicate != modes.end())
    return false;
  modes.push_back(mode);
  return true;
}

float DisplayModeCatalog::NormalizeWindowsRefresh(unsigned int frequency) {
  // SDL's Windows backend interprets these DEVMODE integers as NTSC rates.
  // Normalize BEFORE merging, so 59 (Win32) and 59.94 (SDL) don't appear
  // twice, while 59.94 and a genuine 60.00 remain separately selectable.
  switch (frequency) {
  case 29:
  case 59:
  case 119:
    return static_cast<float>((frequency + 1) * 1000.0 / 1001.0);
  default:
    return static_cast<float>(frequency);
  }
}

void DisplayModeCatalog::Sort(std::vector<DisplayModeChoice> &modes) {
  std::sort(modes.begin(), modes.end(),
            [](const DisplayModeChoice &a, const DisplayModeChoice &b) {
              const auto areaA = static_cast<long long>(a.width) * a.height;
              const auto areaB = static_cast<long long>(b.width) * b.height;
              if (areaA != areaB)
                return areaA > areaB;
              if (a.width != b.width)
                return a.width > b.width;
              if (a.height != b.height)
                return a.height > b.height;
              return a.refreshRate > b.refreshRate;
            });
}

int DisplayModeCatalog::FindSelected(
    const std::vector<DisplayModeChoice> &modes, int width, int height,
    float refreshRate) {
  int sameSize = -1;
  for (size_t i = 0; i < modes.size(); ++i) {
    if (modes[i].width != width || modes[i].height != height)
      continue;
    if (sameSize < 0)
      sameSize = static_cast<int>(i);
    if (std::abs(modes[i].refreshRate - refreshRate) < kRefreshEquality)
      return static_cast<int>(i);
  }
  return sameSize >= 0 ? sameSize : (modes.empty() ? -1 : 0);
}

std::string DisplayModeCatalog::Label(const DisplayModeChoice &mode) {
  char label[96];
  if (std::abs(mode.refreshRate - std::round(mode.refreshRate)) < 0.005f)
    SDL_snprintf(label, sizeof(label), "%d x %d @ %.0f Hz", mode.width,
                 mode.height, mode.refreshRate);
  else
    SDL_snprintf(label, sizeof(label), "%d x %d @ %.2f Hz", mode.width,
                 mode.height, mode.refreshRate);
  return label;
}

bool DisplayModeCatalog::Select(EngineSettings &settings,
                                DisplayModeChoice mode,
                                const SDL_DisplayMode *desktop) {
  if (!Valid(mode))
    return false;
  if (settings.mode == WindowMode::Windowed) {
    settings.width = mode.width;
    settings.height = mode.height;
  } else if (settings.mode == WindowMode::Borderless &&
             (!desktop || desktop->w != mode.width ||
              desktop->h != mode.height ||
              std::abs(desktop->refresh_rate - mode.refreshRate) >=
                  kRefreshEquality)) {
    // Borderless cannot switch the physical display mode. Explicitly choosing
    // another resolution/Hz requests Exclusive, not a silent no-op.
    settings.mode = WindowMode::Exclusive;
  }
  settings.fullscreenWidth = mode.width;
  settings.fullscreenHeight = mode.height;
  settings.refreshRate = mode.refreshRate;
  return true;
}

std::vector<DisplayModeChoice>
DisplayModeCatalog::Enumerate(SDL_DisplayID display) {
  std::vector<DisplayModeChoice> result;
  if (!display)
    return result;

#if defined(_WIN32) && !defined(__ANDROID__)
  // Use the selected SDL monitor's Win32 mode table, NOT a union with the
  // SDL table: DXGI may report slightly different refresh fractions for the
  // same DEVMODE, which would create misleading duplicate choices. This is
  // also the catalog behind the Windows display resolution controls.
  // Monitor number and DISPLAYn are not interchangeable after hotplug.
  const SDL_PropertiesID properties = SDL_GetDisplayProperties(display);
  const auto monitor = static_cast<HMONITOR>(SDL_GetPointerProperty(
      properties, SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER, nullptr));
  if (monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
      for (DWORD index = 0;; ++index) {
        DEVMODEW native{};
        native.dmSize = sizeof(native);
        if (!EnumDisplaySettingsExW(info.szDevice, index, &native, 0))
          break;
        if (native.dmBitsPerPel != 32)
          continue;
        AddUnique(result, {static_cast<int>(native.dmPelsWidth),
                           static_cast<int>(native.dmPelsHeight),
                           NormalizeWindowsRefresh(native.dmDisplayFrequency)});
      }
    }
  }
  if (!result.empty()) {
    Sort(result);
    return result;
  }
#endif

  // SDL fallback (also the only catalog on macOS, Linux and mobile).
  int count = 0;
  SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
  if (modes) {
    for (int i = 0; i < count; ++i)
      if (modes[i])
        AddUnique(result, {modes[i]->w, modes[i]->h, modes[i]->refresh_rate});
    SDL_free(modes);
  }
  if (const auto *desktop = SDL_GetDesktopDisplayMode(display))
    AddUnique(result, {desktop->w, desktop->h, desktop->refresh_rate});

  Sort(result);
  return result;
}

} // namespace Kotonoha
