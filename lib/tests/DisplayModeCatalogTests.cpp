#include <Kotonoha/components/DisplayModeCatalog.hpp>
#include <Kotonoha/components/SelectBox.hpp>
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <tuple>

#if defined(_WIN32) && !defined(__ANDROID__)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Kotonoha.DisplayModeCatalog: " << message << '\n';
    std::exit(1);
  }
}

void ProbeDriverCatalog() {
#if defined(_WIN32) && !defined(__ANDROID__)
  // No SDL window is created and no display mode is changed by this probe.
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cout << "Windows mode probe skipped: " << SDL_GetError() << '\n';
    return;
  }
  const char *driver = SDL_GetCurrentVideoDriver();
  if (driver && std::string(driver) == "windows") {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    Require(displays && count > 0, "SDL Windows backend returned no display");
    const SDL_DisplayID display = displays[0];
    SDL_free(displays);
    const auto catalog = Kotonoha::DisplayModeCatalog::Enumerate(display);
    Require(!catalog.empty(), "Windows display mode catalog is empty");
    const SDL_PropertiesID properties = SDL_GetDisplayProperties(display);
    const HMONITOR monitor = static_cast<HMONITOR>(SDL_GetPointerProperty(
        properties, SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER, nullptr));
    Require(monitor != nullptr, "SDL did not expose the Win32 monitor handle");
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    Require(GetMonitorInfoW(monitor, &info) != 0,
            "Win32 could not resolve the selected SDL monitor");
    int checked = 0;
    std::set<std::tuple<int, int, int>> uniqueNative;
    for (DWORD index = 0;; ++index) {
      DEVMODEW mode{};
      mode.dmSize = sizeof(mode);
      if (!EnumDisplaySettingsExW(info.szDevice, index, &mode, 0))
        break;
      if (mode.dmBitsPerPel != 32 || mode.dmPelsWidth < 320 ||
          mode.dmPelsHeight < 240 || mode.dmDisplayFrequency == 0)
        continue;
      uniqueNative.insert(
          {static_cast<int>(mode.dmPelsWidth),
           static_cast<int>(mode.dmPelsHeight),
           static_cast<int>(std::lround(
               Kotonoha::DisplayModeCatalog::NormalizeWindowsRefresh(
                   mode.dmDisplayFrequency) *
               100.0f))});
      const int found = Kotonoha::DisplayModeCatalog::FindSelected(
          catalog, static_cast<int>(mode.dmPelsWidth),
          static_cast<int>(mode.dmPelsHeight),
          Kotonoha::DisplayModeCatalog::NormalizeWindowsRefresh(
              mode.dmDisplayFrequency));
      Require(
          found >= 0 &&
              catalog[found].width == static_cast<int>(mode.dmPelsWidth) &&
              catalog[found].height == static_cast<int>(mode.dmPelsHeight) &&
              std::abs(catalog[found].refreshRate -
                       Kotonoha::DisplayModeCatalog::NormalizeWindowsRefresh(
                           mode.dmDisplayFrequency)) < 0.02f,
          "Win32 mode missing from the generic selection catalog");
      ++checked;
    }
    Require(checked > 0, "Win32 enumeration produced no modern display modes");
    Require(catalog.size() == uniqueNative.size(),
            "generic catalog duplicated or dropped native Windows modes");
    std::cout << "Windows driver modes represented: " << checked
              << "; catalog entries: " << catalog.size() << '\n';
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
#endif
}

void ProbeScrollableSelector() {
  // All entries must remain keyboard-reachable even when only ten are
  // visible at a time; this is the same selector used by the Settings page.
  std::vector<std::string> choices;
  for (int index = 0; index < 37; ++index)
    choices.push_back(std::to_string(index));
  Kotonoha::SelectBox box;
  int accepted = -1;
  const auto key = [&](SDL_Keycode code) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    box.onEvent(event);
  };
  box.open("Modes", choices, 0, [&](int index) { accepted = index; });
  key(SDLK_END);
  key(SDLK_RETURN);
  Require(accepted == 36 && !box.isOpen(),
          "End could not select the final supported display mode");
  accepted = -1;
  box.open("Modes", choices, 36, [&](int index) { accepted = index; });
  key(SDLK_HOME);
  key(SDLK_PAGEDOWN);
  key(SDLK_RETURN);
  Require(accepted == 10, "Home/PageDown could not reach an intermediate mode");
}

} // namespace

int main() {
  using namespace Kotonoha;
  std::vector<DisplayModeChoice> modes;
  // One monitor can expose many resolution * refresh combinations; none may
  // be replaced by a fixed four-size preset list or deduped merely by size.
  const struct {
    int w, h;
  } sizes[] = {{1920, 1080}, {1366, 768}, {1360, 768}, {1280, 768},
               {1280, 720},  {1176, 664}, {1024, 768}, {800, 600},
               {720, 576},   {720, 480},  {640, 480}};
  for (const auto &size : sizes)
    Require(DisplayModeCatalog::AddUnique(modes, {size.w, size.h, 60.0f}),
            "a Windows-supported resolution was discarded");
  for (float refresh :
       {23.0f, 24.0f, 25.0f, 29.97003f, 30.0f, 50.0f, 59.94006f})
    Require(DisplayModeCatalog::AddUnique(modes, {1920, 1080, refresh}),
            "an alternate supported refresh was discarded");
  Require(modes.size() == 18, "all distinct resolution/refresh tuples survive");
  Require(!DisplayModeCatalog::AddUnique(modes, {1920, 1080, 59.94005f}),
          "two representations of the same refresh were duplicated");
  Require(!DisplayModeCatalog::AddUnique(modes, {1366, 768, 60.0f}),
          "identical resolution and refresh were duplicated");
  Require(std::abs(DisplayModeCatalog::NormalizeWindowsRefresh(59) -
                   59.94006f) < 0.001f &&
              std::abs(DisplayModeCatalog::NormalizeWindowsRefresh(29) -
                       29.97003f) < 0.001f &&
              DisplayModeCatalog::NormalizeWindowsRefresh(60) == 60.0f,
          "Windows fractional refresh normalization mismatches SDL");
  Require(!DisplayModeCatalog::AddUnique(modes, {0, 0, 60.0f}) &&
              !DisplayModeCatalog::AddUnique(modes, {320, 240, 0.0f}),
          "invalid driver mode was accepted");

  DisplayModeCatalog::Sort(modes);
  Require(modes.front().width == 1920 && modes.front().height == 1080,
          "resolutions must be sorted by descending pixel area");
  const int fractional =
      DisplayModeCatalog::FindSelected(modes, 1920, 1080, 59.94006f);
  const int integer = DisplayModeCatalog::FindSelected(modes, 1920, 1080, 60);
  Require(fractional >= 0 && integer >= 0 && fractional != integer,
          "59.94 and 60 must be selectable separately");
  Require(DisplayModeCatalog::Label(modes[fractional]).find("59.94 Hz") !=
                  std::string::npos &&
              DisplayModeCatalog::Label(modes[integer]).find("60 Hz") !=
                  std::string::npos,
          "frequency is missing or incorrectly rounded on the UI");
  Require(DisplayModeCatalog::FindSelected(modes, 1366, 768, 60) >= 0 &&
              DisplayModeCatalog::FindSelected({}, 1366, 768, 60) == -1,
          "current-mode selection fails on an available or empty catalog");

  SDL_DisplayMode desktop{};
  desktop.w = 1366;
  desktop.h = 768;
  desktop.refresh_rate = 60.0f;

  EngineSettings settings;
  settings.mode = WindowMode::Windowed;
  settings.width = 960;
  settings.height = 540;
  Require(DisplayModeCatalog::Select(settings, {1024, 768, 75}, &desktop) &&
              settings.mode == WindowMode::Windowed && settings.width == 1024 &&
              settings.height == 768 && settings.fullscreenWidth == 1024 &&
              settings.fullscreenHeight == 768 && settings.refreshRate == 75,
          "windowed resolution was not saved as real window geometry");
  settings.mode = WindowMode::Borderless;
  Require(DisplayModeCatalog::Select(settings, {1366, 768, 60}, &desktop) &&
              settings.mode == WindowMode::Borderless,
          "native borderless resolution should remain borderless");
  Require(
      DisplayModeCatalog::Select(settings, {1366, 768, 59.94006f}, &desktop) &&
          settings.mode == WindowMode::Exclusive,
      "refresh-only change was silently ignored in borderless mode");
  settings.mode = WindowMode::Borderless;
  Require(DisplayModeCatalog::Select(settings, {1920, 1080, 60}, &desktop) &&
              settings.mode == WindowMode::Exclusive,
          "non-native resolution must request a real exclusive display mode");
  Require(!DisplayModeCatalog::Select(settings, {1920, 1080, 0}, &desktop),
          "invalid display mode was applied");

  ProbeScrollableSelector();
  ProbeDriverCatalog();
  std::cout << "Kotonoha.DisplayModeCatalog PASS\n";
}
