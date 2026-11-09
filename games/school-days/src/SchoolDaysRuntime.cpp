#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/componentes/DisplayModeCatalog.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/localization/Encoding.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <Kotonoha/SchoolDaysAudioEventState.hpp>
#include <Kotonoha/SchoolDaysBgmEventState.hpp>
#include <Kotonoha/SchoolDaysMovieEventState.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/SplashScreen.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNSaveMenu.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <Kotonoha/media/TextureAlpha.hpp>
#include <Kotonoha/mobile/AndroidDisplay.hpp>
#include <Kotonoha/routing/SchoolDaysBackLogEngine.hpp>
#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>
#include <Kotonoha/routing/SchoolDaysDesktopStartupProfile.hpp>
#include <Kotonoha/routing/SchoolDaysOrsFadeBoundary.hpp>
#include <Kotonoha/routing/SchoolDaysOrsFadeFrame.hpp>
#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>
#include <Kotonoha/routing/SchoolDaysPlaybackPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapModel.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapOracle.hpp>
#include <Kotonoha/routing/SchoolDaysSaveMetadata.hpp>
#include <Kotonoha/routing/SchoolDaysSaveService.hpp>
#include <Kotonoha/routing/SchoolDaysSubtitleTypography.hpp>
#include <Kotonoha/routing/SchoolDaysTitleMovieClock.hpp>
#include <Kotonoha/routing/SchoolDaysTitleProfile.hpp>
#include <Kotonoha/routing/SchoolDaysTitleRenderPlan.hpp>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(_WIN32) && !defined(__ANDROID__)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace SchoolDays {
namespace {
using Policy = Kotonoha::SchoolDaysRetailConfigPolicy;
using Ui = Kotonoha::SchoolDaysSystemUi;
using DisplayPolicy = Kotonoha::SchoolDaysDisplayPolicy;
using FrontendTransition = Kotonoha::SchoolDaysFrontendTransition;
constexpr Uint64 StatusMilliseconds = 6000;
constexpr double SchoolDaysMediaCacheSeconds = 2.0;

Kotonoha::SchoolDaysBgmResources
BgmResourcesForPlayback(const Assets &assets, const std::string &path) {
  auto resources = Kotonoha::SchoolDaysBgmResources::Normal(path);
  try {
    (void)assets.resolve(resources.intro);
  } catch (const std::runtime_error &) {
    // Several retail tracks contain only _loop.ogg. Start with that file and
    // keep it as the repeat source instead of dropping the whole BGM event.
    resources.intro = resources.loop;
  }
  return resources;
}

struct FittedViewport {
  float x = 0.0f;
  float y = 0.0f;
  float scale = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

FittedViewport FitToRuntime(int virtualWidth, int virtualHeight) {
  FittedViewport result;
  if (virtualWidth <= 0 || virtualHeight <= 0)
    return result;
  result.scale = std::min(1280.0f / static_cast<float>(virtualWidth),
                          720.0f / static_cast<float>(virtualHeight));
  result.width = virtualWidth * result.scale;
  result.height = virtualHeight * result.scale;
  result.x = (1280.0f - result.width) * 0.5f;
  result.y = (720.0f - result.height) * 0.5f;
  return result;
}

SDL_FRect TransformToRuntime(const SDL_FRect &rect,
                             const FittedViewport &viewport) {
  return {viewport.x + rect.x * viewport.scale,
          viewport.y + rect.y * viewport.scale, rect.w * viewport.scale,
          rect.h * viewport.scale};
}

bool PointerToFitted(float x, float y, int virtualWidth, int virtualHeight,
                     int *virtualX, int *virtualY) {
  if (!virtualX || !virtualY)
    return false;
  const auto viewport = FitToRuntime(virtualWidth, virtualHeight);
  if (viewport.scale <= 0.0f)
    return false;
  const float vx = (x - viewport.x) / viewport.scale;
  const float vy = (y - viewport.y) / viewport.scale;
  if (vx < 0.0f || vy < 0.0f || vx >= virtualWidth || vy >= virtualHeight) {
    *virtualX = -1;
    *virtualY = -1;
    return true;
  }
  *virtualX = static_cast<int>(vx);
  *virtualY = static_cast<int>(vy);
  return true;
}

Kotonoha::SchoolDaysConfigMenuModel::Variant
ConfigVariant(DisplayPolicy::Variant variant) {
  using Out = Kotonoha::SchoolDaysConfigMenuModel::Variant;
  switch (variant) {
  case DisplayPolicy::Variant::Standard:
    return Out::Standard;
  case DisplayPolicy::Variant::Wide:
    return Out::Wide;
  case DisplayPolicy::Variant::WideFull:
    return Out::WideFull;
  case DisplayPolicy::Variant::WideNote:
    return Out::WideNote;
  }
  return Out::Standard;
}

Kotonoha::SchoolDaysExitViewModel::Variant
ExitVariant(DisplayPolicy::Variant variant) {
  using Out = Kotonoha::SchoolDaysExitViewModel::Variant;
  switch (variant) {
  case DisplayPolicy::Variant::Standard:
    return Out::Standard;
  case DisplayPolicy::Variant::Wide:
    return Out::Wide;
  case DisplayPolicy::Variant::WideFull:
    return Out::WideFull;
  case DisplayPolicy::Variant::WideNote:
    return Out::WideNote;
  }
  return Out::Standard;
}

Kotonoha::SchoolDaysBackLogModel::Variant
BackLogVariant(DisplayPolicy::Variant variant) {
  using Out = Kotonoha::SchoolDaysBackLogModel::Variant;
  switch (variant) {
  case DisplayPolicy::Variant::Standard:
    return Out::Standard;
  case DisplayPolicy::Variant::Wide:
    return Out::Wide;
  case DisplayPolicy::Variant::WideFull:
    return Out::WideFull;
  case DisplayPolicy::Variant::WideNote:
    return Out::WideNote;
  }
  return Out::Standard;
}

bool FrontendInputEvent(Uint32 type) {
  return type == SDL_EVENT_MOUSE_MOTION ||
         type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         type == SDL_EVENT_MOUSE_BUTTON_UP || type == SDL_EVENT_MOUSE_WHEEL ||
         type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP ||
         type == SDL_EVENT_GAMEPAD_AXIS_MOTION ||
         type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
         type == SDL_EVENT_GAMEPAD_BUTTON_UP || type == SDL_EVENT_FINGER_DOWN ||
         type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_MOTION;
}

struct DisplayPanelGeometry {
  SDL_FRect tab{};
  SDL_FRect panel{};
  float rowHeight = 0.0f;
};

struct DisplayResolution {
  int width = 0;
  int height = 0;
  float refreshRate = 0.0f;
};

struct DisplayPopupLayout {
  SDL_FRect rect{};
  float headerHeight = 0.0f;
  float rowHeight = 0.0f;
  int visibleRows = 0;
};

// Display Options artwork is always authored in 800x450 coordinates. Standard
// adds 75 canvas units on top, and Standard+FULL deliberately stretches its
// 800x600 CMAP over the entire physical Screen (rather than letterboxing it).
// This must mirror SchoolDaysSystemUi::ComputeConfigViewport, including Y
// scale.
struct DesktopDisplayCanvas {
  float ox = 0.0f, oy = 0.0f, sx = 1.0f, sy = 1.0f;
  SDL_FRect map(float x, float y, float w, float h) const {
    return {ox + x * sx, oy + y * sy, w * sx, h * sy};
  }
};

DesktopDisplayCanvas
DesktopCanvas(Kotonoha::SchoolDaysConfigMenuModel::Variant variant,
              SDL_FPoint output, bool standardFullStretch) {
  const auto layout =
      Kotonoha::SchoolDaysConfigMenuRenderPlan::LayoutFor(variant);
  const float width = std::max(1.0f, output.x);
  const float height = std::max(1.0f, output.y);
  const float fit =
      std::min(width / layout.logicalWidth, height / layout.logicalHeight);
  float px = (width - layout.logicalWidth * fit) * 0.5f;
  float py = (height - layout.logicalHeight * fit) * 0.5f;
  float sx = fit, sy = fit;
  if (standardFullStretch &&
      variant == Kotonoha::SchoolDaysConfigMenuModel::Variant::Standard) {
    px = py = 0.0f;
    sx = width / layout.logicalWidth;
    sy = height / layout.logicalHeight;
  }
  return {px, py + layout.yOffset * layout.scale * sy, layout.scale * sx,
          layout.scale * sy};
}

constexpr float kDesktopPopupX = 245.0f;
constexpr float kDesktopPopupY = 92.0f;
constexpr float kDesktopPopupWidth = 510.0f;
constexpr float kDesktopPopupRow = 29.0f;
constexpr int kDesktopPopupRows = 9;

std::string DisplayClipText(const std::string &value, size_t maxChars) {
  if (value.size() <= maxChars)
    return value;
  return "..." + value.substr(value.size() - maxChars + 3u);
}

DisplayPanelGeometry
DisplayGeometry(Kotonoha::SchoolDaysConfigMenuModel::Variant variant,
                float outputWidth, float outputHeight) {
  const auto layout =
      Kotonoha::SchoolDaysConfigMenuRenderPlan::LayoutFor(variant);
  outputWidth = std::max(outputWidth, 1.0f);
  outputHeight = std::max(outputHeight, 1.0f);
  const float fit =
      std::min(outputWidth / static_cast<float>(layout.logicalWidth),
               outputHeight / static_cast<float>(layout.logicalHeight));
  const float viewportWidth = layout.logicalWidth * fit;
  const float viewportHeight = layout.logicalHeight * fit;
  const float viewportX = (outputWidth - viewportWidth) * 0.5f;
  const float viewportY = (outputHeight - viewportHeight) * 0.5f;
  const float authoredX = (398.0f - 0.5f) * layout.scale;
  const float authoredY = (54.0f - 0.5f + layout.yOffset) * layout.scale;
  const float authoredW = (194.0f + 1.0f) * layout.scale;
  const float authoredH = (38.0f + 1.0f) * layout.scale;
  DisplayPanelGeometry result;
  result.tab = {viewportX + authoredX * fit, viewportY + authoredY * fit,
                authoredW * fit, authoredH * fit};
  const float margin = std::max(14.0f, viewportWidth * 0.018f);
  const float panelY =
      result.tab.y + result.tab.h + std::max(8.0f, viewportHeight * 0.012f);
  result.panel = {viewportX + margin, panelY, viewportWidth - margin * 2.0f,
                  std::max(0.0f, viewportY + viewportHeight - panelY - margin)};
  result.rowHeight = std::min(64.0f, std::max(44.0f, result.panel.h / 7.2f));
  return result;
}

SDL_FRect DisplayRow(const DisplayPanelGeometry &geometry, int row) {
  return {geometry.panel.x + 18.0f,
          geometry.panel.y + 20.0f + row * geometry.rowHeight - 4.0f,
          geometry.panel.w - 36.0f, geometry.rowHeight - 8.0f};
}

DisplayPopupLayout
DisplayResolutionPopup(const DisplayPanelGeometry &geometry) {
  DisplayPopupLayout result;
  result.rowHeight =
      std::min(44.0f, std::max(34.0f, geometry.rowHeight * 0.72f));
  result.headerHeight = result.rowHeight;
  result.rect = {geometry.panel.x + geometry.panel.w * 0.44f,
                 geometry.panel.y + 16.0f, geometry.panel.w * 0.54f - 16.0f,
                 geometry.panel.h - 32.0f};
  result.visibleRows = std::max(
      1, static_cast<int>((result.rect.h - result.headerHeight - 12.0f) /
                          result.rowHeight));
  return result;
}

DisplayPopupLayout DisplayPresetPopup(const DisplayPanelGeometry &geometry) {
  auto result = DisplayResolutionPopup(geometry);
  result.visibleRows = 5;
  result.rect.h =
      result.headerHeight + result.visibleRows * result.rowHeight + 12.0f;
  return result;
}

SDL_DisplayID DisplayIdForIndex(int index) {
  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  if (!displays || count <= 0)
    return 0;
  if (index < 0 || index >= count)
    index = 0;
  const SDL_DisplayID result = displays[index];
  SDL_free(displays);
  return result;
}

std::vector<DisplayResolution> DisplayResolutions(int displayIndex) {
  std::vector<DisplayResolution> result;
  const SDL_DisplayID display = DisplayIdForIndex(displayIndex);
  if (display == 0)
    return result;
  const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
  // Display Options redraws every frame. Windows mode enumeration starts a
  // potentially expensive driver query on index 0; cache the catalog while
  // the selected monitor/desktop mode is stable, with a periodic refresh for
  // external driver/monitor settings changes that preserve desktop geometry.
  struct ResolutionCache {
    SDL_DisplayID display = 0;
    int width = 0;
    int height = 0;
    float refreshRate = 0.0f;
    uint64_t queriedAtMs = 0;
    std::vector<DisplayResolution> values;
  };
  static ResolutionCache cache;
  constexpr uint64_t kModeCacheLifetimeMs = 15000;
  const uint64_t nowMs = SDL_GetTicks();
  if (desktop && cache.display == display && cache.width == desktop->w &&
      cache.height == desktop->h &&
      std::abs(cache.refreshRate - desktop->refresh_rate) < 0.01f &&
      nowMs - cache.queriedAtMs < kModeCacheLifetimeMs)
    return cache.values;
  auto add = [&](int width, int height, float refreshRate) {
    if (width <= 0 || height <= 0)
      return;
    const bool duplicate = std::any_of(
        result.begin(), result.end(), [&](const DisplayResolution &candidate) {
          return candidate.width == width && candidate.height == height &&
                 std::abs(candidate.refreshRate - refreshRate) < 0.75f;
        });
    if (!duplicate)
      result.push_back({width, height, refreshRate});
  };
  int count = 0;
  SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
  if (modes) {
    for (int i = 0; i < count; ++i)
      if (modes[i])
        add(modes[i]->w, modes[i]->h, modes[i]->refresh_rate);
    SDL_free(modes);
  }
  if (desktop)
    add(desktop->w, desktop->h, desktop->refresh_rate);
#if defined(_WIN32) && !defined(__ANDROID__)
  // SDL's fullscreen catalog is often narrower than the Windows Settings
  // resolution picker. Ask the display driver's own Win32 mode table too;
  // resolve SDL's monitor handle instead of assuming Display 1 maps to
  // \\.\DISPLAY1 (that assumption breaks multi-monitor reorder/hotplug).
  const SDL_PropertiesID properties = SDL_GetDisplayProperties(display);
  const HMONITOR monitor = static_cast<HMONITOR>(SDL_GetPointerProperty(
      properties, SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER, nullptr));
  if (monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
      for (DWORD modeIndex = 0;; ++modeIndex) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(info.szDevice, modeIndex, &mode, 0))
          break;
        // Windows exposes low-color legacy modes that the modern SDL
        // renderer cannot enter reliably. Windows Settings presents modern
        // 32bpp resolutions; keep only those and positive refresh rates.
        if (mode.dmBitsPerPel != 32 || mode.dmPelsWidth < 320 ||
            mode.dmPelsHeight < 240 || mode.dmPelsWidth > 16384 ||
            mode.dmPelsHeight > 16384 || mode.dmDisplayFrequency == 0)
          continue;
        add(static_cast<int>(mode.dmPelsWidth),
            static_cast<int>(mode.dmPelsHeight),
            static_cast<float>(mode.dmDisplayFrequency));
      }
    }
  }
#endif
  std::sort(result.begin(), result.end(),
            [](const DisplayResolution &left, const DisplayResolution &right) {
              const long long leftArea =
                  static_cast<long long>(left.width) * left.height;
              const long long rightArea =
                  static_cast<long long>(right.width) * right.height;
              if (leftArea != rightArea)
                return leftArea > rightArea;
              if (left.width != right.width)
                return left.width > right.width;
              if (left.height != right.height)
                return left.height > right.height;
              return left.refreshRate > right.refreshRate;
            });
  if (desktop) {
    cache.display = display;
    cache.width = desktop->w;
    cache.height = desktop->h;
    cache.refreshRate = desktop->refresh_rate;
    cache.queriedAtMs = nowMs;
    cache.values = result;
  }
  return result;
}

#if defined(__ANDROID__) || defined(ANDROID)
constexpr std::array<float, 3> kAndroidRenderScales{1.0f, 0.75f, 0.50f};
constexpr std::array<const char *, 3> kAndroidRenderScaleLabels{
    "Native", "Balanced", "Performance"};

std::vector<Kotonoha::AndroidDisplayMode> AndroidDisplayModesUnique() {
  const auto modes = Kotonoha::AndroidEnumerateDisplayModes();
  std::vector<Kotonoha::AndroidDisplayMode> result;
  const int currentId = Kotonoha::AndroidCurrentDisplayModeId();
  int currentWidth = 0, currentHeight = 0;
  for (const auto &mode : modes)
    if (mode.modeId == currentId) {
      currentWidth = mode.width;
      currentHeight = mode.height;
      break;
    }
  const long long currentArea =
      static_cast<long long>(currentWidth) * currentHeight;
  for (const auto &mode : modes) {
    if (mode.modeId <= 0 || mode.width <= 0 || mode.height <= 0 ||
        mode.refreshRate <= 0.0f)
      continue;
    const auto duplicate =
        std::find_if(result.begin(), result.end(), [&](const auto &candidate) {
          return std::abs(candidate.refreshRate - mode.refreshRate) < 0.01f;
        });
    if (duplicate == result.end()) {
      result.push_back(mode);
      continue;
    }
    const bool oldCurrent =
        duplicate->width == currentWidth && duplicate->height == currentHeight;
    const bool newCurrent =
        mode.width == currentWidth && mode.height == currentHeight;
    if (newCurrent && !oldCurrent) {
      *duplicate = mode;
    } else if (newCurrent == oldCurrent && currentArea > 0) {
      const long long oldArea =
          static_cast<long long>(duplicate->width) * duplicate->height;
      const long long newArea =
          static_cast<long long>(mode.width) * mode.height;
      if (std::llabs(newArea - currentArea) < std::llabs(oldArea - currentArea))
        *duplicate = mode;
    }
  }
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return a.refreshRate < b.refreshRate;
  });
  return result;
}

void FormatAndroidFrameRate(const Kotonoha::AndroidDisplayMode &mode,
                            char *buffer, size_t size) {
  if (!buffer || size == 0)
    return;
  const float rounded = std::round(mode.refreshRate);
  if (std::abs(mode.refreshRate - rounded) < 0.01f)
    SDL_snprintf(buffer, size, "%.0f FPS", rounded);
  else
    SDL_snprintf(buffer, size, "%.2f FPS", mode.refreshRate);
}

int SelectedAndroidMode(const std::vector<Kotonoha::AndroidDisplayMode> &modes,
                        const Kotonoha::EngineSettings &settings) {
  if (modes.empty())
    return -1;
  if (settings.fullscreenWidth > 0 && settings.fullscreenHeight > 0 &&
      settings.refreshRate > 0.0f) {
    for (size_t i = 0; i < modes.size(); ++i)
      if (modes[i].width == settings.fullscreenWidth &&
          modes[i].height == settings.fullscreenHeight &&
          std::abs(modes[i].refreshRate - settings.refreshRate) < 0.05f)
        return static_cast<int>(i);
  }
  const int current = Kotonoha::AndroidCurrentDisplayModeId();
  for (size_t i = 0; i < modes.size(); ++i)
    if (modes[i].modeId == current)
      return static_cast<int>(i);
  return 0;
}

DisplayPopupLayout
AndroidRenderScalePopup(const DisplayPanelGeometry &geometry) {
  auto popup = DisplayResolutionPopup(geometry);
  popup.visibleRows = static_cast<int>(kAndroidRenderScales.size());
  popup.rect.h =
      popup.headerHeight + popup.visibleRows * popup.rowHeight + 12.0f;
  return popup;
}
#endif

void FormatDisplayResolution(const DisplayResolution &resolution, char *buffer,
                             size_t bufferSize) {
  if (!buffer || bufferSize == 0)
    return;
  const float rounded = std::round(resolution.refreshRate);
  if (std::abs(resolution.refreshRate - rounded) < 0.01f)
    SDL_snprintf(buffer, bufferSize, "%dx%d @ %.0f Hz", resolution.width,
                 resolution.height, resolution.refreshRate);
  else
    SDL_snprintf(buffer, bufferSize, "%dx%d @ %.2f Hz", resolution.width,
                 resolution.height, resolution.refreshRate);
}

int SelectedDisplayResolution(const std::vector<DisplayResolution> &resolutions,
                              const Kotonoha::EngineSettings &settings) {
  if (resolutions.empty())
    return -1;
  // The selected label must reflect the actual Windowed dimensions, not a
  // stale fullscreen tuple retained for the next switch into Full.
  const bool windowed = settings.mode == Kotonoha::WindowMode::Windowed;
  const int width = !windowed && settings.fullscreenWidth > 0
                        ? settings.fullscreenWidth
                        : settings.width;
  const int height = !windowed && settings.fullscreenHeight > 0
                         ? settings.fullscreenHeight
                         : settings.height;
  if (settings.refreshRate > 0.0f)
    for (size_t i = 0; i < resolutions.size(); ++i)
      if (resolutions[i].width == width && resolutions[i].height == height &&
          std::abs(resolutions[i].refreshRate - settings.refreshRate) < 0.05f)
        return static_cast<int>(i);
  for (size_t i = 0; i < resolutions.size(); ++i)
    if (resolutions[i].width == width && resolutions[i].height == height)
      return static_cast<int>(i);
  const SDL_DisplayID display = DisplayIdForIndex(settings.display);
  if (const auto *desktop =
          display ? SDL_GetDesktopDisplayMode(display) : nullptr) {
    for (size_t i = 0; i < resolutions.size(); ++i)
      if (resolutions[i].width == desktop->w &&
          resolutions[i].height == desktop->h &&
          std::abs(resolutions[i].refreshRate - desktop->refresh_rate) < 0.05f)
        return static_cast<int>(i);
  }
  return 0;
}

bool PointIn(float x, float y, const SDL_FRect &rect) {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.w &&
         y < rect.y + rect.h;
}

constexpr const char *DisplayPresetLabels[]{
    "OFF", "Anime Light - ArtCNN C4F16", "Anime Clean - ArtCNN C4F16 DS",
    "Anime Quality - ArtCNN C4F32", "Legacy - Anime4K B-S"};

int CurrentDisplayPreset(const Kotonoha::EngineSettings &settings) {
  if (!settings.upscaling.enabled())
    return 0;
  switch (settings.upscaling.preset) {
  case Kotonoha::UpscalePreset::ArtCNN:
    return 1;
  case Kotonoha::UpscalePreset::ArtCNNDenoise:
    return 2;
  case Kotonoha::UpscalePreset::ArtCNNLarge:
    return 3;
  case Kotonoha::UpscalePreset::Anime4K:
  case Kotonoha::UpscalePreset::Anime4KModeBHQ:
  case Kotonoha::UpscalePreset::Anime4KModeCHQ:
  case Kotonoha::UpscalePreset::Anime4KModeAAHQ:
  case Kotonoha::UpscalePreset::Anime4KModeBBHQ:
  case Kotonoha::UpscalePreset::Anime4KModeCAHQ:
  case Kotonoha::UpscalePreset::Anime4KModeAFast:
  case Kotonoha::UpscalePreset::Anime4KModeBFast:
  case Kotonoha::UpscalePreset::Anime4KModeCFast:
  case Kotonoha::UpscalePreset::Anime4KModeAAFast:
  case Kotonoha::UpscalePreset::Anime4KModeBBFast:
  case Kotonoha::UpscalePreset::Anime4KModeCAFast:
    return 4;
  }
  return 4;
}

const char *WindowModeLabel(Kotonoha::WindowMode mode) {
  switch (mode) {
  case Kotonoha::WindowMode::Windowed:
    return "Windowed";
  case Kotonoha::WindowMode::Borderless:
    return "Borderless";
  case Kotonoha::WindowMode::Exclusive:
    return "Exclusive";
  }
  return "Windowed";
}

std::filesystem::path
RemasterDisplaySettingsPath(const std::filesystem::path &documents) {
  return documents / "Kotonoha.ini";
}

int LoadRemasterAspectPreference(const std::filesystem::path &documents) {
  std::ifstream input(RemasterDisplaySettingsPath(documents));
  if (!input)
    input.open(documents / "RemasterDisplay.ini");
  std::string line;
  while (std::getline(input, line)) {
    constexpr std::string_view prefix = "ContentAspectPreference=";
    if (line.rfind(prefix.data(), 0) != 0)
      continue;
    try {
      return std::clamp(std::stoi(line.substr(prefix.size())), 0, 2);
    } catch (const std::exception &) {
      return 0;
    }
  }
  return 0;
}

void SaveRemasterAspectPreference(const std::filesystem::path &documents,
                                  int preference) {
  std::string contents;
  {
    std::ifstream input(RemasterDisplaySettingsPath(documents));
    contents.assign(std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
  }
  const std::string key = "ContentAspectPreference=";
  const std::string value = key + std::to_string(std::clamp(preference, 0, 2));
  const size_t begin = contents.find(key);
  if (begin != std::string::npos &&
      (begin == 0 || contents[begin - 1] == '\n')) {
    const size_t end = contents.find_first_of("\r\n", begin);
    contents.replace(
        begin, end == std::string::npos ? contents.size() - begin : end - begin,
        value);
  } else {
    if (!contents.empty() && contents.back() != '\n')
      contents.push_back('\n');
    if (contents.empty())
      contents = "[KotonohaConfig][Settings]\n";
    contents += value + "\n";
  }
  Kotonoha::writeTextFileAtomically(
      RemasterDisplaySettingsPath(documents).string(), contents);
}

void Check(bool ok, const Kotonoha_KtrfError &error) {
  if (!ok)
    throw std::runtime_error(error.message[0] ? error.message
                                              : "School Days routing failed");
}

void Check(bool ok, const std::string &error, const char *fallback) {
  if (!ok)
    throw std::runtime_error(error.empty() ? fallback : error);
}

void WriteBytes(const std::string &path, const std::vector<uint8_t> &bytes) {
  Kotonoha::writeTextFileAtomically(path,
                                    std::string(bytes.begin(), bytes.end()));
}

struct Presentation final : Kotonoha::SchoolDaysScenePresentation {
  Kotonoha_orsData data{};
  bool choiceCommitted = false;
  ~Presentation() override { Kotonoha_OrsClean(&data); }
  void RestoreChoice(int64_t) override { choiceCommitted = true; }
  void SetPaused(bool) override {}
};

const char *Text(const char *text) { return text ? text : ""; }

std::u16string Utf16(const std::string &text) {
  if (text.empty())
    return {};
  char *converted =
      SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(), text.size() + 1);
  if (!converted)
    return {};
  std::u16string result(reinterpret_cast<const char16_t *>(converted));
  SDL_free(converted);
  return result;
}

struct TtfFont {
  TTF_Font *font = nullptr;
  ~TtfFont() {
    if (font)
      TTF_CloseFont(font);
  }
};
} // namespace

// The worker owns no ORS event or SDL renderer object. Only the runtime thread
// maps completed ordinals back to the current scene's event list.
class AbcAnalysisWorker {
public:
  struct Result {
    uint64_t generation = 0;
    size_t ordinal = 0;
    bool media = false;
    bool background = false;
    std::vector<Kotonoha::SchoolDaysAbcActivityRecord> activity;
    std::unique_ptr<Kotonoha::MediaPlayer::PreparedInput> mediaInput;
    std::unique_ptr<Kotonoha::MediaPlayer::PreparedInput> loopInput;
    std::string backgroundResource;
    Kotonoha::SurfacePtr backgroundSurface;
    std::vector<Kotonoha::SchoolDaysAbcGroupResources> backgroundGroups;
    std::vector<std::pair<std::string, Kotonoha::SurfacePtr>>
        backgroundAbcSurfaces;
  };
  explicit AbcAnalysisWorker(Assets &source, uint64_t generation)
      : assets(source), currentGeneration(generation),
        worker([this] { run(); }) {}
  ~AbcAnalysisWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
      cancellation->store(true);
      pending.clear();
    }
    wake.notify_one();
    worker.join();
  }
  void reset(uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex);
    cancellation->store(true);
    cancellation = std::make_shared<std::atomic_bool>(false);
    currentGeneration = generation;
    pending.clear();
    completed.clear();
    wake.notify_one();
  }
  bool enqueue(uint64_t generation, size_t ordinal, std::string logical,
               uint64_t durationMs) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping || generation != currentGeneration || pending.size() >= 8u)
      return false;
    pending.push_back({generation,
                       ordinal,
                       std::move(logical),
                       {},
                       {},
                       durationMs,
                       false,
                       false,
                       false,
                       cancellation});
    wake.notify_one();
    return true;
  }
  bool enqueueMedia(uint64_t generation, size_t ordinal, std::string logical,
                    bool audioOnly, std::string loop) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping || generation != currentGeneration || pending.size() >= 8u)
      return false;
    pending.push_back({generation,
                       ordinal,
                       std::move(logical),
                       std::move(loop),
                       {},
                       0,
                       true,
                       false,
                       audioOnly,
                       cancellation});
    wake.notify_one();
    return true;
  }
  bool enqueueBackground(uint64_t generation, size_t ordinal,
                         std::string logical,
                         std::vector<std::string> abcKeys) {
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping || generation != currentGeneration || pending.size() >= 8u)
      return false;
    pending.push_back({generation,
                       ordinal,
                       std::move(logical),
                       {},
                       std::move(abcKeys),
                       0,
                       false,
                       true,
                       false,
                       cancellation});
    wake.notify_one();
    return true;
  }
  std::vector<Result> drain() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Result> result;
    result.reserve(completed.size());
    while (!completed.empty()) {
      result.push_back(std::move(completed.front()));
      completed.pop_front();
    }
    wake.notify_one();
    return result;
  }

private:
  struct Job {
    uint64_t generation;
    size_t ordinal;
    std::string logical;
    std::string loop;
    std::vector<std::string> abcKeys;
    uint64_t durationMs;
    bool media;
    bool background;
    bool audioOnly;
    std::shared_ptr<std::atomic_bool> cancelled;
  };
  void run() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [this] { return stopping || !pending.empty(); });
        if (stopping)
          return;
        job = std::move(pending.front());
        pending.pop_front();
      }
      Result result;
      result.generation = job.generation;
      result.ordinal = job.ordinal;
      result.media = job.media;
      result.background = job.background;
      try {
        if (!job.cancelled->load()) {
          const auto resolved = assets.resolve(job.logical, job.background);
          if (job.background) {
            result.backgroundResource = resolved;
            result.backgroundSurface = assets.loadSurface(resolved);
            const size_t slash = resolved.find_last_of("/\\");
            const auto directory = slash == std::string::npos
                                       ? std::string()
                                       : resolved.substr(0, slash + 1u);
            result.backgroundGroups = Kotonoha::SchoolDaysGroupAbcSiblings(
                resolved, assets.listAsset(directory));
            result.backgroundGroups.erase(
                std::remove_if(
                    result.backgroundGroups.begin(),
                    result.backgroundGroups.end(),
                    [](const auto &group) { return group.collision; }),
                result.backgroundGroups.end());
            for (const auto &group : result.backgroundGroups) {
              if (std::find(job.abcKeys.begin(), job.abcKeys.end(),
                            group.key) == job.abcKeys.end())
                continue;
              for (const auto &path : group.paths) {
                if (job.cancelled->load())
                  break;
                if (path.empty())
                  continue;
                try {
                  auto decoded = assets.loadSurface(path);
                  Kotonoha::SurfacePtr rgba(SDL_ConvertSurface(
                      decoded.get(), SDL_PIXELFORMAT_RGBA32));
                  if (!rgba)
                    throw std::runtime_error(SDL_GetError());
                  (void)Kotonoha::prepareStraightAlphaForLinearFiltering(
                      static_cast<std::uint8_t *>(rgba->pixels), rgba->w,
                      rgba->h, rgba->pitch);
                  result.backgroundAbcSurfaces.emplace_back(path,
                                                            std::move(rgba));
                } catch (const std::exception &error) {
                  SDL_LogWarn(Kotonoha::LogUI, "School Days ABC surface %s: %s",
                              path.c_str(), error.what());
                }
              }
            }
          } else if (job.media) {
            auto input = Kotonoha::MediaPlayer::prepareInput(
                assets, resolved, job.audioOnly, job.cancelled);
            if (!job.loop.empty() && !job.cancelled->load())
              result.loopInput = Kotonoha::MediaPlayer::prepareInput(
                  assets, assets.resolve(job.loop), true, job.cancelled);
            if (!job.cancelled->load())
              result.mediaInput = std::move(input);
          } else {
            const auto bytes = assets.read(resolved);
            if (!job.cancelled->load()) {
              std::vector<std::int16_t> pcm;
              if (Kotonoha::MediaDecoder::decodeMonoS16At44100(
                      bytes, pcm, job.cancelled.get(), job.durationMs))
                result.activity =
                    Kotonoha::SchoolDaysAnalyzeAbcPcmActivity(pcm);
            }
          }
        }
      } catch (const std::exception &error) {
        const char *operation = job.background ? "background prefetch"
                                : job.media    ? "media prefetch"
                                               : "ABC analysis";
        SDL_LogWarn(Kotonoha::LogUI, "School Days %s %s: %s", operation,
                    job.logical.c_str(), error.what());
      }
      std::unique_lock<std::mutex> lock(mutex);
      wake.wait(lock, [&] {
        return stopping || job.cancelled->load() ||
               result.generation != currentGeneration || completed.size() < 8u;
      });
      if (!stopping && !job.cancelled->load() &&
          result.generation == currentGeneration)
        completed.push_back(std::move(result));
    }
  }
  Assets &assets;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<Job> pending;
  std::deque<Result> completed;
  std::shared_ptr<std::atomic_bool> cancellation =
      std::make_shared<std::atomic_bool>(false);
  uint64_t currentGeneration = 0;
  bool stopping = false;
  std::thread worker;
};

Runtime::Runtime(std::shared_ptr<Assets> source, std::string route,
                 std::filesystem::path saveDirectory)
    : Kotonoha::Runtime("School Days HQ", "0.1.0",
                        "me.hirameki.kotonoha.schooldays"),
      assets(std::move(source)), systemUi(assets.get()),
      backlogPresenter(assets.get()), routePath(std::move(route)),
      documents(std::move(saveDirectory)) {
  Kotonoha_KtrfError error{};
  Check(bridge.Open(routePath.c_str(), assets->orsRoot().string().c_str(),
                    nullptr, {createPresentation, destroyPresentation, this},
                    &error),
        error);
}

Runtime::~Runtime() { shutdown(); }

Kotonoha::Gameplay *
Runtime::createPresentation(const char *path, Kotonoha_Game *, void *userdata) {
  auto &self = *static_cast<Runtime *>(userdata);
  try {
    const auto bytes = self.assets->read(path);
    auto presentation = std::make_unique<Presentation>();
    presentation->data = Kotonoha_OrsParserBytes(bytes.data(), bytes.size());
    if (!presentation->data.size)
      throw std::runtime_error("Empty or invalid ORS: " + std::string(path));
    return presentation.release();
  } catch (const std::exception &error) {
    self.setStatus(error.what());
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
    return nullptr;
  }
}

void Runtime::destroyPresentation(Kotonoha::Gameplay *presentation, void *) {
  delete presentation;
}

bool Runtime::resolveReplayScene(const char *sceneKey,
                                 Kotonoha::SchoolDaysKtrfScene *resolved,
                                 void *userdata, Kotonoha_KtrfError *error) {
  if (!userdata || !sceneKey || !resolved)
    return false;
  auto &self = *static_cast<Runtime *>(userdata);
  return self.bridge.Session().ResolveSceneKey(sceneKey, resolved, error);
}

void Runtime::shutdown() {
  // Join before the asset mount and decoder dependencies are released.
  abcAnalysisWorker.reset();
  resetPreparedMedia();
  abcActivityCache.clear();
  abcVoices.clear();
  abcGroups.clear();
  abcTextures.clear();
  abcDecodedSurfaces.clear();
  resetCarryFrame();
  resetBackgroundComposition();
  if (configStorage.IsLoaded()) {
    std::string configError;
    // Native SysMenu writes mutate a cache; orderly shutdown is the physical
    // persistence boundary. A clean first-run cache therefore remains absent.
    if (!configStorage.Flush(&configError))
      SDL_LogWarn(Kotonoha::LogUI, "School Days Config.DAT flush: %s",
                  configError.c_str());
  }
  if (bridge.IsOpen() && !documents.empty()) {
    try {
      flushGlobal();
    } catch (const std::exception &error) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
    }
  }
  playing.clear();
  movieStates.clear();
  moveSom.Reset();
  replayPlayback.reset();
  playDataPlayback.reset();
  sounds.clear();
  titleBgm.reset();
  frontendBgmOwner = FrontendBgmOwner::None;
  titleMovie.reset();
  ClearScenes();
  // Panel textures belong to the engine renderer: release them first.
  systemUi.Close();
  backlogModel.Close();
  backlogPresenter.Close();
  subtitles.reset();
  gameScene.reset();
  engineSplash.reset();
  logoSplash.reset();
  saveMenu.reset();
  backlogFallback.reset();
  assets->clearCache();
  bridge.Close();
  script = {};
  engine = nullptr;
}

void Runtime::prepareCarryFrame() {
  if (engine && engine->GetVisualTransition().hasLastPresented() &&
      !engine->GetVisualTransition().active()) {
    if (engine->GetVisualTransition().begin("narrative-boundary",
                                            scene.sceneKey))
      traceDev("OutgoingCaptured");
  }
}

void Runtime::commitCarryFrame() {
  if (!engine)
    return;
  auto &transition = engine->GetVisualTransition();
  if (!transition.active()) {
    if (!transition.hasLastPresented())
      return;
    transition.begin("narrative-boundary", scene.sceneKey);
  }
  transition.arm(scene.sceneKey);
}

void Runtime::cancelPreparedCarryFrame() {
  if (engine)
    engine->GetVisualTransition().cancel();
}

void Runtime::resetCarryFrame() {
  if (engine)
    engine->GetVisualTransition().reset();
}

bool Runtime::devPauseRequested() const { return false; }

bool Runtime::devConsumeStep() { return false; }

void Runtime::devIdle() { lastClock = SDL_GetTicks(); }

void Runtime::devControlPump(Kotonoha::RendererRef renderer) { (void)renderer; }

void Runtime::devInputProcessed(Kotonoha::EventRef event) { (void)event; }

void Runtime::traceDev(const char *event) { (void)event; }

void Runtime::init(Kotonoha::Engine &owner) {
  if (engine != &owner) {
    engine = &owner;
    engine->GetDebug().getSettings().showFPS = false;
    engine->GetDebug().getSettings().showNodeClock = false;
#if defined(_WIN32) && !defined(__ANDROID__)
    // Only a fresh profile tracks the monitor's desktop resolution. A chosen
    // size is persisted in settings.json and must survive subsequent launches
    // even if the screen's current desktop mode has since changed.
    {
      const auto settingsBefore = engine->GetSettings();
      if (!engine->HasStoredSettings() || settingsBefore.fullscreenWidth <= 0 ||
          settingsBefore.fullscreenHeight <= 0 ||
          settingsBefore.refreshRate <= 0.0f) {
        const SDL_DisplayID display = DisplayIdForIndex(settingsBefore.display);
        if (const auto *desktop =
                display ? SDL_GetDesktopDisplayMode(display) : nullptr) {
          auto settings = settingsBefore;
          if (Kotonoha::SchoolDaysDesktopStartupProfile::Apply(
                  settings, engine->HasStoredSettings(), desktop->w, desktop->h,
                  desktop->refresh_rate))
            engine->applySettings(settings);
        }
      }
    }
#endif
    if (documents.empty()) {
#if defined(_WIN32) && !defined(__ANDROID__)
      // The commercial Windows build keeps Config.DAT, GlobalFlag and Save
      // beside SchoolDays.exe. Preserve that profile so a base/100% commercial
      // state directory remains self-contained; --documents still overrides it.
      if (const char *base = SDL_GetBasePath()) {
        documents = std::filesystem::path(base);
      } else {
        throw std::runtime_error(std::string("School Days base path: ") +
                                 SDL_GetError());
      }
#else
      documents = Kotonoha::preferredDocumentsDirectory("school-days");
#endif
    } else {
      std::filesystem::create_directories(documents);
    }
#if defined(__ANDROID__) || defined(ANDROID)
    // One-time migration to the commercial mobile-low profile.  A marker keeps
    // later user choices (Native/OFF/etc.) authoritative instead of resetting
    // them on every launch.
    const auto mobileProfileMarker = documents / "RemasterMobileProfile.v1";
    if (!std::filesystem::is_regular_file(mobileProfileMarker) &&
        !engine->HasStoredSettings()) {
      try {
        auto settings = engine->GetSettings();
        settings.mode = Kotonoha::WindowMode::Borderless;
        settings.vsync = true;
        settings.renderScale = 0.75f;
        settings.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
        settings.upscaling.preset = Kotonoha::UpscalePreset::ArtCNN;
        settings.upscaling.factor = 2.0f;
        const auto modes = AndroidDisplayModesUnique();
        if (!modes.empty()) {
          const auto selected =
              std::min_element(modes.begin(), modes.end(),
                               [](const auto &left, const auto &right) {
                                 return std::abs(left.refreshRate - 60.0f) <
                                        std::abs(right.refreshRate - 60.0f);
                               });
          settings.fullscreenWidth = selected->width;
          settings.fullscreenHeight = selected->height;
          settings.refreshRate = selected->refreshRate;
        }
        engine->applySettings(settings);
        Kotonoha::writeTextFileAtomically(mobileProfileMarker.string(), "1\n");
      } catch (const std::exception &error) {
        SDL_LogWarn(Kotonoha::LogUI,
                    "School Days Android mobile profile migration: %s",
                    error.what());
      }
    }
#endif
    subtitles = std::make_unique<Subtitles>(owner);
    gameScene = std::make_unique<GameScene>(owner, *this);
    logoSplash = std::make_unique<LogoSplash>(owner, *assets);
    saveMenu = std::make_unique<Kotonoha::VNSaveMenu>(owner);
    backlogFallback =
        std::make_unique<Kotonoha::SchoolDaysBackLogEngine>(owner, *assets);

    // FILMENGINE.INI drives the retail save layout, FONTDATA and system SFX.
    std::string failure;
    hasProfile = Kotonoha::SchoolDaysSaveProfile::Load(assets->root().string(),
                                                       &profile, &failure);
    if (hasProfile && !storage.Initialize(documents, profile, &failure))
      SDL_LogWarn(Kotonoha::LogUI, "School Days save storage: %s",
                  failure.c_str());
    if (!hasProfile)
      SDL_LogInfo(
          Kotonoha::LogUI,
          "School Days FILMENGINE.INI unavailable (%s); using engine save menu",
          failure.c_str());

    std::vector<uint8_t> bytes;
    bool present = false;
    if (storage.IsInitialized() &&
        !storage.LoadGlobalFlag(&bytes, &present, &failure))
      present = false;
    const auto legacy = documents / "GlobalFlag";
    if (!present && std::filesystem::is_regular_file(legacy)) {
      bytes = assets->read(legacy.string());
      present = true;
    }
    if (!present) {
      // A clean first launch has no Save/GlobalFlag.DAT yet. Keep a live,
      // empty FlagsHash so the first save can create the retail document.
      if (!Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
              Kotonoha::SchoolDaysFlgH{}, &bytes, &failure))
        throw std::runtime_error(failure);
      present = true;
    }
    if (present) {
      Kotonoha_KtrfError error{};
      Check(bridge.Session().LoadGlobalFlag(bytes, &error), error);
    }
    loadConfig();
    applyConfig();
    lastClock = SDL_GetTicks();
  }
  Kotonoha::Runtime::init(owner);
}

void Runtime::initGlobal(Kotonoha::Engine &) {
  AddScene(subtitles.get(), Kotonoha::GameState::Gameplay);
  AddScene(gameScene.get(), Kotonoha::GameState::Gameplay);
  AddScene(backlogFallback.get(), Kotonoha::GameState::Gameplay);
  AddScene(saveMenu.get(), Kotonoha::GameState::Menu);
  AddScene(saveMenu.get(), Kotonoha::GameState::Gameplay);
}

void Runtime::initSplash(Kotonoha::Engine &owner) {
  // Same order as the original port: engine SPLASH.PNG, then the retail logo.
  if (!engineSplash)
    engineSplash =
        std::make_unique<Kotonoha::SplashScreen>(owner, splashPictures());
  else
    engineSplash->restart();
  logoSplash->restart();
  AddScene(engineSplash.get(), Kotonoha::GameState::Splash);
}

void Runtime::initMenu(Kotonoha::Engine &) { openTitle(); }

void Runtime::finishSplash() {
  SetState(Kotonoha::GameState::Menu);
  Kotonoha::Runtime::init(*engine);
}

void Runtime::setStatus(std::string message) {
  status = std::move(message);
  statusTime = SDL_GetTicks();
}

bool Runtime::panelOpen() const {
  return systemUi.IsOpen() || backlogModel.IsOpen() ||
         (saveMenu && saveMenu->isOpen()) ||
         (backlogFallback && backlogFallback->isVisible()) ||
         (engine && engine->GetSettingsMenu().isOpen());
}

bool Runtime::modal() const {
  return panelOpen() || loadTransition.Active() || exitTransition.Active() ||
         systemPanelTransition.Active() || (engine && engine->isStoryPaused());
}

void Runtime::closePanels() {
  discardDesktopDisplayDraft();
  systemUi.Close();
  backlogModel.Close();
  backlogPresenter.Close();
  if (saveMenu && saveMenu->isOpen())
    saveMenu->close();
  if (backlogFallback && backlogFallback->isVisible())
    backlogFallback->close();
  saveLoadPage = routeMapMajor = routeMapSubpage = -1;
  saveLoadOwner = SaveLoadOwner::None;
  backlogOwner = BackLogOwner::None;
}

const std::string &Runtime::frontendBgmStem() const {
  static const std::string empty;
  switch (frontendBgmOwner) {
  case FrontendBgmOwner::Title:
    return titleProfile.titleBgmStem;
  case FrontendBgmOwner::System:
    return titleProfile.systemBgmStem;
  case FrontendBgmOwner::None:
    return empty;
  }
  return empty;
}

bool Runtime::startFrontendBgm(FrontendBgmOwner owner) {
  if (titleBgm && frontendBgmOwner == owner)
    return true;
  titleBgm.reset();
  frontendBgmOwner = owner;
  const std::string &stem = frontendBgmStem();
  if (!engine || stem.empty())
    return true;
  try {
    titleBgm = std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
    titleBgm->setBufferSeconds(SchoolDaysMediaCacheSeconds);
    titleBgm->open(*assets, assets->resolve(stem + "_loop.ogg"), true, "BGM");
    return true;
  } catch (const std::exception &) {
    titleBgm.reset();
    return false;
  }
}

void Runtime::suspendFrontendBgm() { titleBgm.reset(); }

void Runtime::beginTitleHandoff(FrontendHandoff handoff, uint64_t nowMs) {
  if (frontendHandoff != FrontendHandoff::None ||
      frontendTransition.Active(nowMs))
    return;
  using Profile = FrontendTransition::Profile;
  switch (handoff) {
  case FrontendHandoff::TitleStart:
    frontendTransition.Begin(Profile::TitleToStart, nowMs);
    break;
  case FrontendHandoff::TitleLoad:
  case FrontendHandoff::TitleConfig:
  case FrontendHandoff::TitleReplay:
    frontendTransition.Begin(Profile::TitleToChild, nowMs);
    break;
  default:
    return;
  }
  frontendHandoff = handoff;
}

void Runtime::beginChildToTitle(uint64_t nowMs) {
  if (frontendChild == FrontendChild::None ||
      frontendHandoff != FrontendHandoff::None ||
      frontendTransition.Active(nowMs))
    return;
  switch (frontendChild) {
  case FrontendChild::Load:
    frontendHandoff = FrontendHandoff::LoadToTitle;
    break;
  case FrontendChild::Config:
    frontendHandoff = FrontendHandoff::ConfigToTitle;
    break;
  case FrontendChild::Replay:
    frontendHandoff = FrontendHandoff::ReplayToTitle;
    break;
  case FrontendChild::None:
    return;
  }
  frontendTransition.Begin(FrontendTransition::Profile::ChildToTitle, nowMs);
}

void Runtime::updateFrontendTransition(uint64_t nowMs) {
  if (!frontendTransition.NeedsHandoff(nowMs)) {
    frontendTransition.Update(nowMs);
    if (replayHSceneTransitionActive && !frontendTransition.Active(nowMs))
      replayHSceneTransitionActive = false;
    return;
  }

  if (pendingReplayIndex >= 0) {
    const int index = pendingReplayIndex;
    const int choiceIndex = pendingReplayChoice;
    pendingReplayIndex = -1;
    pendingReplayChoice = -1;
    try {
      startReplayPlayback(index, choiceIndex);
      frontendChild = FrontendChild::None;
      frontendTransition.CompleteHandoff(SDL_GetTicks());
    } catch (const std::exception &error) {
      frontendTransition.Reset();
      replayHSceneTransitionActive = false;
      setStatus(error.what());
      SDL_LogError(Kotonoha::LogUI, "School Days H-scene handoff failed: %s",
                   error.what());
    }
    return;
  }

  const FrontendHandoff handoff = frontendHandoff;
  switch (handoff) {
  case FrontendHandoff::TitleStart:
    frontendChild = FrontendChild::None;
    reset();
    break;
  case FrontendHandoff::TitleLoad:
    frontendChild = FrontendChild::Load;
    openSaveLoad(false, false);
    break;
  case FrontendHandoff::TitleConfig:
    frontendChild = FrontendChild::Config;
    openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def, false);
    break;
  case FrontendHandoff::TitleReplay:
    frontendChild = FrontendChild::Replay;
    if (!openReplay())
      frontendChild = FrontendChild::None;
    break;
  case FrontendHandoff::LoadToTitle:
  case FrontendHandoff::ConfigToTitle:
  case FrontendHandoff::ReplayToTitle:
    frontendChild = FrontendChild::None;
    openTitle();
    break;
  case FrontendHandoff::None:
    frontendTransition.CompleteHandoff(nowMs);
    return;
  }
  frontendHandoff = FrontendHandoff::None;
  frontendTransition.CompleteHandoff(nowMs);
}

void Runtime::renderFrontendTransition(Kotonoha::RendererRef renderer,
                                       uint64_t nowMs) {
  if (!frontendTransition.Active(nowMs))
    return;
  const Uint8 alpha = frontendTransition.Alpha(nowMs);
  if (alpha == 0u)
    return;
  int virtualWidth = 1280;
  int virtualHeight = 720;
  switch (uiVariant) {
  case DisplayPolicy::Variant::Standard:
    virtualWidth = 800;
    virtualHeight = 600;
    break;
  case DisplayPolicy::Variant::Wide:
    virtualWidth = 800;
    virtualHeight = 450;
    break;
  case DisplayPolicy::Variant::WideFull:
    virtualWidth = 1280;
    virtualHeight = 720;
    break;
  case DisplayPolicy::Variant::WideNote:
    virtualWidth = 1024;
    virtualHeight = 576;
    break;
  }
  const auto viewport = FitToRuntime(virtualWidth, virtualHeight);
  const SDL_FRect area{viewport.x, viewport.y, viewport.width, viewport.height};
  SDL_BlendMode previousBlend = SDL_BLENDMODE_NONE;
  Uint8 r = 0, g = 0, b = 0, a = 0;
  (void)SDL_GetRenderDrawBlendMode(&renderer, &previousBlend);
  (void)SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a);
  (void)SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  const Uint8 value =
      frontendTransition.CurrentColor() == FrontendTransition::Color::White
          ? 255u
          : 0u;
  (void)SDL_SetRenderDrawColor(&renderer, value, value, value, alpha);
  (void)SDL_RenderFillRect(&renderer, &area);
  (void)SDL_SetRenderDrawColor(&renderer, r, g, b, a);
  (void)SDL_SetRenderDrawBlendMode(&renderer, previousBlend);
}

void Runtime::updateLoadTransition(uint64_t nowMs) {
  if (loadTransition.Update(nowMs) !=
      Kotonoha::SchoolDaysLoadTransition::Event::RestoreSlot)
    return;
  try {
    // Restore only after the opaque loading card has covered the old LOAD
    // owner. Keep the existing synchronous persistence transaction intact.
    if (loadTransition.Target() ==
        Kotonoha::SchoolDaysLoadTransition::Destination::ReplayData)
      startPlayDataPlayback(loadTransition.Slot());
    else
      loadSlot(loadTransition.Slot());
    loadTransition.Restored(SDL_GetTicks());
  } catch (const std::exception &error) {
    loadTransition.Reset();
    setStatus(error.what());
    SDL_LogError(Kotonoha::LogUI, "School Days LOAD restore failed: %s",
                 error.what());
  }
}

void Runtime::renderLoadTransition(Kotonoha::RendererRef renderer,
                                   uint64_t nowMs) {
  using Phase = Kotonoha::SchoolDaysLoadTransition::Phase;
  const Phase phase = loadTransition.CurrentPhase();
  if (phase == Phase::Idle)
    return;

  SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
  Uint8 oldRed = 0, oldGreen = 0, oldBlue = 0, oldAlpha = 0;
  (void)SDL_GetRenderDrawBlendMode(&renderer, &oldBlend);
  (void)SDL_GetRenderDrawColor(&renderer, &oldRed, &oldGreen, &oldBlue,
                               &oldAlpha);
  (void)SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);

  if (phase == Phase::ShowLoading || phase == Phase::FadeOutLoading) {
    (void)SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    (void)SDL_RenderFillRect(&renderer, nullptr);
    try {
      // This is the actual 800x450 retail loading card stored in System.gpk.
      // Resolve through the user's mounted archives; never bundle its pixels.
      auto &texture = assets->texture(
          renderer, assets->resolve("System/SAVELOAD/POPUP_LOAD", true));
      const SDL_FPoint screen = engine->screenSize();
      const float scale = std::min(screen.x / 800.0f, screen.y / 450.0f);
      const SDL_FRect dest{(screen.x - 800.0f * scale) * 0.5f,
                           (screen.y - 450.0f * scale) * 0.5f, 800.0f * scale,
                           450.0f * scale};
      (void)SDL_RenderTexture(&renderer, &texture, nullptr, &dest);
    } catch (const std::exception &error) {
      SDL_LogWarn(Kotonoha::LogUI,
                  "School Days retail loading card unavailable: %s",
                  error.what());
    }
  }

  const Uint8 alpha = loadTransition.BlackAlpha(nowMs);
  if (alpha != 0u) {
    (void)SDL_SetRenderDrawColor(&renderer, 0, 0, 0, alpha);
    (void)SDL_RenderFillRect(&renderer, nullptr);
  }
  (void)SDL_SetRenderDrawColor(&renderer, oldRed, oldGreen, oldBlue, oldAlpha);
  (void)SDL_SetRenderDrawBlendMode(&renderer, oldBlend);
}

void Runtime::beginTransportFade(
    Kotonoha::SchoolDaysTransportFade::Operation operation) {
  if (currentState != Kotonoha::GameState::Gameplay || panelOpen() ||
      pendingSeek || pendingBackCapture || exitTransition.Active() ||
      systemPanelTransition.Active() || transportFade.Active())
    return;
  if (operation == Kotonoha::SchoolDaysTransportFade::Operation::SkipToEnd &&
      !timeline.ComputeSkipToEndTarget(tick))
    return;
  (void)transportFade.Begin(operation, SDL_GetTicks());
}

void Runtime::updateTransportFade(uint64_t nowMs) {
  using Fade = Kotonoha::SchoolDaysTransportFade;
  if (transportFade.Update(nowMs) != Fade::Event::Navigate)
    return;
  try {
    if (transportFade.CurrentOperation() == Fade::Operation::Rewind) {
      back();
    } else if (auto target = timeline.ComputeSkipToEndTarget(tick)) {
      seek(*target);
    }
    transportFade.Navigated();
  } catch (const std::exception &error) {
    transportFade.Reset();
    setStatus(error.what());
    SDL_LogError(Kotonoha::LogUI, "School Days transport fade seek failed: %s",
                 error.what());
  }
}

void Runtime::renderTransportFade(Kotonoha::RendererRef renderer,
                                  uint64_t nowMs) {
  if (!transportFade.Active())
    return;
  const Uint8 alpha = transportFade.BlackAlpha(nowMs);
  if (!alpha)
    return;
  SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
  Uint8 r = 0, g = 0, b = 0, a = 0;
  (void)SDL_GetRenderDrawBlendMode(&renderer, &oldBlend);
  (void)SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a);
  (void)SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  (void)SDL_SetRenderDrawColor(&renderer, 0, 0, 0, alpha);
  (void)SDL_RenderFillRect(&renderer, nullptr);
  (void)SDL_SetRenderDrawColor(&renderer, r, g, b, a);
  (void)SDL_SetRenderDrawBlendMode(&renderer, oldBlend);
}

void Runtime::updateExitTransition(uint64_t nowMs) {
  using Transition = Kotonoha::SchoolDaysExitTransition;
  const auto target = exitTransition.CurrentTarget();
  switch (exitTransition.Update(nowMs)) {
  case Transition::Event::None:
  case Transition::Event::Closed:
    return;
  case Transition::Event::OpenDialog:
    openExitDialog(target == Transition::Target::Title);
    return;
  case Transition::Event::Confirmed:
    if (target == Transition::Target::Title) {
      finishMainMenuReturn();
      frontendTransition.BeginIncoming(FrontendTransition::Color::Black, 500u,
                                       nowMs);
    } else {
      closePanels();
      exitConfirmedByPlayer = true;
      SDL_Event quit{};
      quit.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit);
    }
    return;
  }
}

void Runtime::renderExitTransition(Kotonoha::RendererRef renderer,
                                   uint64_t nowMs) {
  using Transition = Kotonoha::SchoolDaysExitTransition;
  const auto phase = exitTransition.CurrentPhase();
  if (phase != Transition::Phase::Opening &&
      phase != Transition::Phase::Closing &&
      phase != Transition::Phase::Confirming)
    return;
  int virtualWidth = 1280, virtualHeight = 720;
  switch (uiVariant) {
  case DisplayPolicy::Variant::Standard:
    virtualWidth = 800;
    virtualHeight = 600;
    break;
  case DisplayPolicy::Variant::Wide:
    virtualWidth = 800;
    virtualHeight = 450;
    break;
  case DisplayPolicy::Variant::WideFull:
    break;
  case DisplayPolicy::Variant::WideNote:
    virtualWidth = 1024;
    virtualHeight = 576;
    break;
  }
  // renderScreen uses physical output coordinates. Match SystemUi's fitted
  // viewport so SYSBASE and the popup cover the same pixels at any resolution.
  const SDL_FPoint screen = engine->screenSize();
  const float scale = std::min(screen.x / static_cast<float>(virtualWidth),
                               screen.y / static_cast<float>(virtualHeight));
  const SDL_FRect area{(screen.x - virtualWidth * scale) * 0.5f,
                       (screen.y - virtualHeight * scale) * 0.5f,
                       virtualWidth * scale, virtualHeight * scale};
  if (phase == Transition::Phase::Confirming) {
    SDL_BlendMode previousBlend = SDL_BLENDMODE_NONE;
    Uint8 r = 0, g = 0, b = 0, a = 0;
    (void)SDL_GetRenderDrawBlendMode(&renderer, &previousBlend);
    (void)SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a);
    (void)SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    (void)SDL_SetRenderDrawColor(&renderer, 0, 0, 0,
                                 exitTransition.Alpha(nowMs));
    (void)SDL_RenderFillRect(&renderer, &area);
    (void)SDL_SetRenderDrawColor(&renderer, r, g, b, a);
    (void)SDL_SetRenderDrawBlendMode(&renderer, previousBlend);
    return;
  }
  try {
    auto &texture = assets->texture(
        renderer, assets->resolve("System/SYSTEM/SYSBASE", true));
    Uint8 previousAlpha = 255u;
    (void)SDL_GetTextureAlphaMod(&texture, &previousAlpha);
    (void)SDL_SetTextureAlphaMod(&texture, exitTransition.Alpha(nowMs));
    const float fraction = exitTransition.RevealedFraction(nowMs);
    const SDL_FRect source{0, 0, 800.0f, 450.0f * fraction};
    const SDL_FRect destination{area.x, area.y, area.w, area.h * fraction};
    (void)SDL_RenderTexture(&renderer, &texture, &source, &destination);
    (void)SDL_SetTextureAlphaMod(&texture, previousAlpha);
  } catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days SYSBASE transition: %s",
                error.what());
  }
}

void Runtime::beginSystemPanel(
    Kotonoha::SchoolDaysSystemPanelTransition::Target target) {
  if (currentState != Kotonoha::GameState::Gameplay || panelOpen() ||
      exitTransition.Active() || systemPanelTransition.Active())
    return;
  systemPanelTransition.Begin(target, SDL_GetTicks());
}

bool Runtime::beginSystemPanelClose() {
  using Phase = Kotonoha::SchoolDaysSystemPanelTransition::Phase;
  if (currentState != Kotonoha::GameState::Gameplay ||
      systemPanelTransition.CurrentPhase() != Phase::Open)
    return false;
  systemPanelTransition.BeginClose(SDL_GetTicks());
  return true;
}

void Runtime::updateSystemPanelTransition(uint64_t nowMs) {
  using Transition = Kotonoha::SchoolDaysSystemPanelTransition;
  if (systemPanelTransition.CurrentPhase() == Transition::Phase::Open &&
      !panelOpen()) {
    systemPanelTransition.Reset();
    return;
  }
  switch (systemPanelTransition.Update(nowMs)) {
  case Transition::Event::None:
    return;
  case Transition::Event::OpenPanel:
    try {
      switch (systemPanelTransition.CurrentTarget()) {
      case Transition::Target::Save:
        openSaveLoad(true, false);
        break;
      case Transition::Target::Load:
        openSaveLoad(false, false);
        break;
      case Transition::Target::BackLog:
        openBacklog(false);
        break;
      case Transition::Target::Config:
        openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def, false);
        break;
      }
      if (!panelOpen())
        systemPanelTransition.Reset();
    } catch (...) {
      systemPanelTransition.Reset();
      throw;
    }
    return;
  case Transition::Event::ClosePanel:
    closePanels();
    return;
  }
}

void Runtime::renderSystemPanelTransition(Kotonoha::RendererRef renderer,
                                          uint64_t nowMs) {
  using Phase = Kotonoha::SchoolDaysSystemPanelTransition::Phase;
  const auto phase = systemPanelTransition.CurrentPhase();
  if (phase != Phase::Opening && phase != Phase::Closing &&
      phase != Phase::Revealing)
    return;
  SDL_BlendMode previousBlend = SDL_BLENDMODE_NONE;
  Uint8 r = 0, g = 0, b = 0, a = 0;
  (void)SDL_GetRenderDrawBlendMode(&renderer, &previousBlend);
  (void)SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a);
  (void)SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
  if (systemPanelTransition.OpeningFromBlack()) {
    (void)SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    (void)SDL_RenderFillRect(&renderer, nullptr);
  }
  (void)SDL_SetRenderDrawColor(&renderer, 255, 255, 255,
                               systemPanelTransition.WhiteAlpha(nowMs));
  (void)SDL_RenderFillRect(&renderer, nullptr);
  (void)SDL_SetRenderDrawColor(&renderer, r, g, b, a);
  (void)SDL_SetRenderDrawBlendMode(&renderer, previousBlend);
}

bool Runtime::pointerToTitle(float x, float y, int *virtualX,
                             int *virtualY) const {
  if (!title.IsOpen())
    return false;
  return PointerToFitted(x, y, static_cast<int>(title.Cmap().Width()),
                         static_cast<int>(title.Cmap().Height()), virtualX,
                         virtualY);
}

bool Runtime::pointerToBackLog(float x, float y, int *virtualX,
                               int *virtualY) const {
  if (!backlogModel.IsOpen())
    return false;
  return PointerToFitted(x, y, static_cast<int>(backlogModel.Cmap().Width()),
                         static_cast<int>(backlogModel.Cmap().Height()),
                         virtualX, virtualY);
}

void Runtime::openTitle(bool startBgm) {
  transportFade.Reset();
  choiceSpeedOverrideActive = false;
  setPlaybackSpeedIndex(0);
  resetCarryFrame();
  resetPreparedMedia();
  abcActivityCache.clear();
  abcVoices.clear();
  abcGroups.clear();
  abcTextures.clear();
  abcDecodedSurfaces.clear();
  const bool preserveTitleBgm =
      startBgm && titleBgm && frontendBgmOwner == FrontendBgmOwner::Title;
  playing.clear();
  movieStates.clear();
  moveSom.Reset();
  episodeOverlay.Reset();
  episodeTelop.clear();
  titleMovie.reset();
  titleMovieStartMs = 0;
  titleBackground.clear();
  titleBackgroundHeight = 450.0f;
  replayPlayback.reset();
  playDataPlayback.reset();
  script = {};
  timeline.Bind(&script);
  caption = choice = fade = nullptr;
  background.clear();
  replaySelectResult = -2;
  backAtSceneStart = false;
  if (subtitles)
    subtitles->clear();
  closePanels();
  if (!preserveTitleBgm)
    titleBgm.reset();
  frontendBgmOwner = FrontendBgmOwner::Title;
  titleArtwork = true;
  currentState = Kotonoha::GameState::Menu;
  Kotonoha::SchoolDaysTitleModel::HostState host;
  Kotonoha_KtrfError error{};
  int64_t endClear = 0;
  Check(bridge.Session().State().ReadGlobalInt("EndClear", &endClear, &error),
        error);
  host.endClearReplay = endClear != 0;
  std::string ignored;
  if (Kotonoha::SchoolDaysTitleProfile::Load(assets->root().string(),
                                             &titleProfile, &ignored)) {
    host.endClearVisual = titleProfile.endBgView && endClear != 0;
    Kotonoha::SchoolDaysEndBackgroundResolver::Result background;
    Kotonoha::KtrfStateStore::Snapshot stateBefore;
    Check(bridge.Session().State().Capture(&stateBefore, &error), error);
    const auto persistenceBefore = bridge.Session().Persistence();
    if (Kotonoha::SchoolDaysEndBackgroundResolver::Resolve(
            titleProfile, &bridge.Session().State(), &background, &error)) {
      if (background.promotedAllClear &&
          !bridge.Session().Persistence().WriteGlobalInteger(
              "AllClear", 11, -1, &bridge.Session().State(), &error)) {
        Kotonoha_KtrfError ignored{};
        (void)bridge.Session().State().Restore(stateBefore, &ignored);
        bridge.Session().Persistence() = persistenceBefore;
        Check(false, error);
      }
      std::string extension =
          std::filesystem::path(background.asset).extension().string();
      std::transform(
          extension.begin(), extension.end(), extension.begin(),
          [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (extension == ".png") {
        try {
          titleBackground = assets->resolve(background.asset);
          titleBackgroundHeight =
              background.kind ==
                      Kotonoha::SchoolDaysEndBackgroundResolver::Kind::Base
                  ? 450.0f
                  : 452.0f;
        } catch (const std::exception &) {
          titleBackground.clear();
        }
      } else {
        // Retail keeps BaseFile only as the WMV open/decode fallback. A healthy
        // movie owns the background until EOF and retains its final frame.
        try {
          titleBackground = assets->resolve(titleProfile.baseAsset);
        } catch (const std::exception &) {
          titleBackground.clear();
        }
        try {
          titleMovie =
              std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
          titleMovie->setBufferSeconds(SchoolDaysMediaCacheSeconds);
          titleMovie->prepare(*assets, assets->resolve(background.asset), false,
                              "Movie");
          titleMovieStartMs = SDL_GetTicks();
          titleBackgroundHeight = 452.0f;
        } catch (const std::exception &movieError) {
          SDL_LogWarn(Kotonoha::LogUI,
                      "School Days Title EndBG movie failed; using BaseFile "
                      "fallback: %s",
                      movieError.what());
          titleMovie.reset();
          titleMovieStartMs = 0;
          titleBackgroundHeight = 450.0f;
        }
      }
    }
    if (startBgm)
      (void)startFrontendBgm(FrontendBgmOwner::Title);
  }
  title.Open(assets->root().string(), uiVariant, host, &ignored);
  if (startBgm) {
    const uint64_t nowMs = SDL_GetTicks();
    if (frontendHandoff == FrontendHandoff::None &&
        !frontendTransition.Active(nowMs)) {
      frontendTransition.BeginIncoming(titleHasPresented
                                           ? FrontendTransition::Color::White
                                           : FrontendTransition::Color::Black,
                                       titleHasPresented ? 500u : 1000u, nowMs);
    }
    titleHasPresented = true;
  }
}

bool Runtime::temporaryPlaybackActive() const {
  return (replayPlayback && replayPlayback->IsOpen()) ||
         (playDataPlayback && playDataPlayback->IsOpen());
}

bool Runtime::refreshReplayAvailability() {
  if (!systemUi.IsReplayOpen())
    return true;
  auto &state = bridge.Session().State();
  for (int i = 0; i < Kotonoha::SchoolDaysReplayModel::HSceneUnlockCount();
       ++i) {
    const char *key = Kotonoha::SchoolDaysReplayModel::HSceneUnlockKey(i);
    int64_t value = 0;
    Kotonoha_KtrfError error{};
    if (!key || !state.ReadGlobalInt(key, &value, &error)) {
      setStatus(std::string("Replay unlock state unavailable: ") +
                (key ? key : "<null>"));
      return false;
    }
    systemUi.SetReplayHSceneAvailable(i, value != 0);
  }

  if (!storage.IsInitialized()) {
    setStatus("Replay Data storage is unavailable");
    return false;
  }
  std::vector<Kotonoha::SchoolDaysSaveService::SlotMetadata> metadata(100u);
  for (int slot = 0; slot < 100; ++slot) {
    bool openable = false;
    std::string failure;
    if (!storage.ProbeSaveFile(slot, &openable, &failure)) {
      setStatus(failure);
      return false;
    }
    Kotonoha_KtrfError error{};
    if (!Kotonoha::SchoolDaysSaveService::ReadSlotMetadata(
            profile, slot, openable, bridge.Session().Persistence(),
            &metadata[static_cast<size_t>(slot)], &error)) {
      setStatus(error.message);
      return false;
    }
    systemUi.SetReplayPlayDataAvailable(
        slot, metadata[static_cast<size_t>(slot)].occupied);
  }
  std::string failure;
  if (!systemUi.SetReplayPlayDataMetadata(metadata, &failure)) {
    setStatus(failure);
    return false;
  }

  if (systemUi.Replay().SceneCheckOpen()) {
    const int selector = systemUi.Replay().SceneCheckKey();
    for (int choiceIndex = 0; choiceIndex < 4; ++choiceIndex) {
      const char *key =
          Kotonoha::SchoolDaysReplayModel::SceneCheckAvailabilityKey(
              selector, choiceIndex);
      bool available = false;
      if (key) {
        int64_t value = 0;
        Kotonoha_KtrfError error{};
        if (!state.ReadGlobalInt(key, &value, &error)) {
          setStatus(std::string("Replay choice state unavailable: ") + key);
          return false;
        }
        available = value != 0;
      }
      systemUi.SetReplaySceneCheckChoiceAvailable(choiceIndex, available);
    }
  }
  return true;
}

bool Runtime::openReplay() {
  traceDev("ReplayOpen");
  if (!engine || !hasProfile || !storage.IsInitialized()) {
    setStatus(
        "Replay gallery requires the retail School Days save/profile data");
    return false;
  }
  std::string failure;
  if (!systemUi.OpenReplay(&engine->GetRenderer(), assets->root().string(),
                           uiVariant, profile.textInput, profile.useEnglish,
                           profile.fontData, &failure)) {
    setStatus(failure);
    return false;
  }
  if (!refreshReplayAvailability()) {
    systemUi.Close();
    return false;
  }
  if (!startFrontendBgm(FrontendBgmOwner::System)) {
    systemUi.Close();
    (void)startFrontendBgm(FrontendBgmOwner::Title);
    setStatus("Replay SystemBGM could not be started");
    return false;
  }
  return true;
}

void Runtime::startReplayPlayback(int index, int choiceIndex) {
  if (temporaryPlaybackActive())
    throw std::runtime_error("School Days Replay playback is already active");
  auto playback = std::make_unique<Kotonoha::SchoolDaysReplayPlaybackSession>();
  Kotonoha::SchoolDaysReplayPlaybackSession::Resolver resolver{
      &Runtime::resolveReplayScene, this};
  const Kotonoha::SchoolDaysKtrfGameplayBridge::Factory factory{
      &Runtime::createPresentation, &Runtime::destroyPresentation, this};
  Kotonoha_KtrfError error{};
  const bool started =
      choiceIndex >= 0
          ? playback->StartSpecialChoice(index, choiceIndex, nullptr, resolver,
                                         factory, &error)
          : playback->StartOrdinary(index, nullptr, resolver, factory, &error);
  Check(started, error);
  closePanels();
  suspendFrontendBgm();
  replayPlayback = std::move(playback);
  bindScene(replayPlayback->CurrentScene(), replayPlayback->CurrentGameplay());
}

void Runtime::beginReplayHScene(int index, int choiceIndex) {
  // Retail: fade the Replay gallery to black, create the H-scene only at the
  // opaque handoff, then reveal it from black. This is distinct from Replay
  // Data, whose path uses the retail "Please wait. Now loading." card.
  const uint64_t nowMs = SDL_GetTicks();
  if (index < 0 || pendingReplayIndex >= 0 || loadTransition.Active() ||
      frontendTransition.Active(nowMs))
    return;
  pendingReplayIndex = index;
  pendingReplayChoice = choiceIndex;
  replayHSceneTransitionActive = true;
  frontendTransition.Begin(FrontendTransition::Profile::ReplayToHScene, nowMs);
}

void Runtime::startPlayDataPlayback(int slot) {
  traceDev("PlayDataOpen");
  if (temporaryPlaybackActive())
    throw std::runtime_error("School Days Replay playback is already active");
  if (!storage.IsInitialized())
    throw std::runtime_error("School Days Replay Data storage is unavailable");
  std::vector<uint8_t> saveBytes;
  std::string failure;
  Check(storage.ReadSaveFile(slot, &saveBytes, &failure), failure,
        "School Days Replay Data slot read failed");
  std::vector<uint8_t> globalBytes;
  Kotonoha_KtrfError error{};
  Check(bridge.Session().SerializeGlobalFlag(&globalBytes, &error), error);
  auto playback = std::make_unique<Kotonoha::SchoolDaysPlayDataSession>();
  const Kotonoha::SchoolDaysKtrfGameplayBridge::Factory factory{
      &Runtime::createPresentation, &Runtime::destroyPresentation, this};
  Check(playback->Start(routePath.c_str(), assets->orsRoot().string().c_str(),
                        saveBytes, &globalBytes, nullptr, factory, &error),
        error);
  Check(playback->Session().Persistence().ReplaceCurrentScriptSnapshot(
            playback->CurrentScene().sceneKey, UINT32_C(0x3f800000),
            playback->Session().State(), &error),
        error);
  closePanels();
  suspendFrontendBgm();
  playDataBacklogStore.ResetTransient();
  playDataBacklogIndex = 0;
  playDataBacklogScene.clear();
  playDataPlayback = std::move(playback);
  bindScene(playDataPlayback->CurrentScene(),
            playDataPlayback->CurrentGameplay());
}

void Runtime::finishTemporaryPlayback(bool returnToReplay) {
  pendingReplayIndex = -1;
  pendingReplayChoice = -1;
  replayHSceneTransitionActive = false;
  resetPreparedMedia();
  abcActivityCache.clear();
  abcVoices.clear();
  abcGroups.clear();
  abcTextures.clear();
  abcDecodedSurfaces.clear();
  playing.clear();
  replayPlayback.reset();
  playDataPlayback.reset();
  playDataBacklogStore.ResetTransient();
  playDataBacklogIndex = 0;
  playDataBacklogScene.clear();
  script = {};
  timeline.Bind(&script);
  caption = choice = fade = nullptr;
  background.clear();
  replaySelectResult = -2;
  if (returnToReplay) {
    openTitle(false);
    if (!openReplay())
      (void)startFrontendBgm(FrontendBgmOwner::Title);
  } else {
    openTitle();
  }
}

void Runtime::openScene(const Kotonoha::SchoolDaysKtrfScene &next,
                        bool restoreChoice) {
  bindScene(next, bridge.CurrentGameplay(), restoreChoice);
}

void Runtime::bindScene(const Kotonoha::SchoolDaysKtrfScene &next,
                        Kotonoha::Gameplay *gameplay, bool restoreChoice) {
  // EXE 0x4251D0 restores the pre-SETSELECT effective speed after a choice
  // handoff. Keep the selected speed index unchanged for the persistent chip.
  if (choiceSpeedOverrideActive) {
    choiceSpeedOverrideActive = false;
    setPlaybackSpeedIndex(speedIndex);
  }
  // All Gameplay owner replacements share one visual transaction, including
  // Replay, PlayData and private Load paths that do not call advance().
  if (engine && currentState == Kotonoha::GameState::Gameplay &&
      !engine->GetVisualTransition().active())
    prepareCarryFrame();
  pendingSceneAdvance = false;
  pendingBackCapture = false;
  resetPreparedMedia();
  titleBgm.reset();
  if (!temporaryPlaybackActive())
    frontendBgmOwner = FrontendBgmOwner::None;
  titleMovie.reset();
  titleMovieStartMs = 0;
  episodeOverlay.Reset();
  episodeTelop.clear();
  playing.clear();
  movieStates.clear();
  moveSom.Reset();
  backAtSceneStart = false;
  assets->clearCache();
  auto *presentation = static_cast<Presentation *>(gameplay);
  if (!presentation)
    throw std::runtime_error("No School Days scene presentation");
  scene = next;
  script = presentation->data;
  timeline.Bind(&script);
  resetPreparedMedia();
  for (auto *event = script.data; event != nullptr; event = event->next)
    event->eventPrepared = false;
  if (subtitles)
    subtitles->bind(script, scene.sceneKey);
  // Native ScriptChange truncates abandoned-branch BackLog messages. Replay
  // Data owns an isolated transient history; ordinary HScene Replay owns none.
  const bool playData = playDataPlayback && playDataPlayback->IsOpen();
  const bool hostModalOpen = systemUi.IsOpen() || backlogModel.IsOpen() ||
                             (backlogFallback && backlogFallback->isVisible());
  if (playData && !hostModalOpen && !scene.sceneKey.empty() &&
      scene.sceneKey != playDataBacklogScene) {
    playDataBacklogStore.ObserveScriptChange(scene.sceneKey);
    playDataBacklogScene = scene.sceneKey;
  } else if (!temporaryPlaybackActive() && !scene.sceneKey.empty() &&
             scene.sceneKey != backlogScene) {
    backlogStore.ObserveScriptChange(scene.sceneKey);
    backlogScene = scene.sceneKey;
  }
  caption = choice = nullptr;
  choicePresentation = {};
  choiceInteraction.ResetSelection();
  choiceInteraction.SetAutoMode(config.autoMode);
  background.clear();
  backgroundStartTick = backgroundEndTick = 0;
  abcGroups.clear();
  abcVoices.clear();
  abcActivityCache.clear();
  abcTextures.clear();
  abcDecodedSurfaces.clear();
  resetBackgroundComposition();
  committed = restoreChoice || presentation->choiceCommitted;
  replaySelectResult = -2;
  paused = false;
  elapsed = 0;
  tick = 0;
  lastClock = SDL_GetTicks();
  currentState = Kotonoha::GameState::Gameplay;
  if (engine && engine->GetVisualTransition().active())
    engine->GetVisualTransition().arm(scene.sceneKey);
  traceDev("SceneBound");
}

void Runtime::reset() {
  resetCarryFrame();
  retireSceneEventPointers();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.ResetNewGame(&result, &error), error);
  backlogStore.ResetTransient();
  if (backlogFallback)
    backlogFallback->clear();
  backlogIndex = 0;
  backlogScene.clear();
  openScene(result.scene);
}

bool Runtime::play(Kotonoha_orsEvent &event, const std::string &logical,
                   const std::string &channel, const std::string &loop) {
  // Preparation is speculative. A failed worker/adoption must never suppress
  // the authored START event; the direct resolver/player remains the fallback.
  mediaPreparationFailed.erase(&event);
  try {
    std::unique_ptr<Kotonoha::MediaPlayer::PreparedInput> loopInput;
    auto player = takePreparedMedia(event, logical, channel, loop, &loopInput);
    if (!player) {
      player = std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
      player->setBufferSeconds(SchoolDaysMediaCacheSeconds);
      player->setPlaybackRate(speed);
      player->open(*assets, assets->resolve(logical), channel != "Movie",
                   channel);
    }
    Playing item;
    item.player = std::move(player);
    item.preparedLoop = std::move(loopInput);
    item.event = &event;
    item.loop = loop;
    item.segmentStart = event.startTick / 24.0;
    playing.push_back(std::move(item));
    return true;
  } catch (const std::exception &error) {
    setStatus(error.what());
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
    return false;
  }
}

void Runtime::resetPreparedMedia() {
  pendingSeek.reset();
  ++abcAnalysisGeneration;
  abcAnalysisQueued.clear();
  mediaPreparationFailed.clear();
  pendingBackgrounds.clear();
  backgroundPreparationFailed.clear();
  if (abcAnalysisWorker)
    abcAnalysisWorker->reset(abcAnalysisGeneration);
  preparedMedia.clear();
  pendingMedia.clear();
  preparedBackgrounds.clear();
  preparedBackgroundAbc.clear();
  preparedBackgroundAbcSurfaces.clear();
}

void Runtime::retireSceneEventPointers() {
  resetPreparedMedia();
  playing.clear();
  movieStates.clear();
  abcActivityCache.clear();
  abcVoices.clear();
  abcGroups.clear();
  abcTextures.clear();
  abcDecodedSurfaces.clear();
}

std::unique_ptr<Kotonoha::MediaPlayer> Runtime::takePreparedMedia(
    Kotonoha_orsEvent &event, const std::string &logical,
    const std::string &channel, const std::string &loop,
    std::unique_ptr<Kotonoha::MediaPlayer::PreparedInput> *loopInput) {
  const auto found = preparedMedia.find(&event);
  if (found == preparedMedia.end())
    return {};
  if (found->second.logical != logical || found->second.channel != channel ||
      found->second.loop != loop) {
    preparedMedia.erase(found);
    return {};
  }
  auto player = std::move(found->second.player);
  if (loopInput)
    *loopInput = std::move(found->second.loopInput);
  preparedMedia.erase(found);
  if (player) {
    player->setPlaybackRate(speed);
    player->setPaused(false);
  }
  return player;
}

void Runtime::prepareAbcActivity(Kotonoha_orsEvent &event,
                                 const std::string &logical) {
  if (abcActivityCache.count(&event) != 0 ||
      Kotonoha::SchoolDaysNormalizeAbcAnimationKey(
          Kotonoha::SchoolDaysVoiceAnimationKey(&event))
          .empty())
    return;
  size_t ordinal = 0;
  auto *candidate = script.data;
  while (candidate && candidate != &event) {
    candidate = candidate->next;
    ++ordinal;
  }
  if (!candidate || !abcAnalysisQueued.insert(ordinal).second)
    return;
  if (!abcAnalysisWorker)
    abcAnalysisWorker =
        std::make_unique<AbcAnalysisWorker>(*assets, abcAnalysisGeneration);
  const uint64_t durationMs = std::max<uint64_t>(
      1u,
      Kotonoha_SceneTickToMillisecondsCeil(event.endTick - event.startTick));
  if (!abcAnalysisWorker->enqueue(abcAnalysisGeneration, ordinal, logical,
                                  durationMs))
    abcAnalysisQueued.erase(ordinal);
}

void Runtime::pollAbcActivity() {
  if (!abcAnalysisWorker)
    return;
  for (auto &result : abcAnalysisWorker->drain()) {
    if (result.generation != abcAnalysisGeneration)
      continue;
    auto *event = script.data;
    for (size_t index = 0; event && index < result.ordinal; ++index)
      event = event->next;
    if (!event)
      continue;
    if (result.background) {
      pendingBackgrounds.erase(result.ordinal);
      if (result.backgroundResource.empty() || !result.backgroundSurface) {
        backgroundPreparationFailed.insert(event);
        continue;
      }
      try {
        (void)assets->stageTexture(engine->GetRenderer(),
                                   result.backgroundResource,
                                   std::move(result.backgroundSurface));
        preparedBackgrounds[event] = std::move(result.backgroundResource);
        preparedBackgroundAbc[event] = std::move(result.backgroundGroups);
        preparedBackgroundAbcSurfaces[event] =
            std::move(result.backgroundAbcSurfaces);
        traceDev("MediaPrepareEnd:Background");
      } catch (const std::exception &error) {
        SDL_LogWarn(Kotonoha::LogUI, "School Days background adoption: %s",
                    error.what());
        backgroundPreparationFailed.insert(event);
      }
      continue;
    }
    if (result.media) {
      auto pending = pendingMedia.find(result.ordinal);
      if (pending == pendingMedia.end())
        continue;
      if (!result.mediaInput) {
        mediaPreparationFailed.insert(event);
        pendingMedia.erase(pending);
        continue;
      }
      try {
        auto player =
            std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
        player->setPlaybackRate(speed);
        player->adoptPreparedInput(std::move(result.mediaInput),
                                   pending->second.channel);
        // Bound the render-thread handoff. Demux/codec setup happened on the
        // worker; prime only a short queue/frame here, then restore the
        // commercial two-second steady-state media cache.
        player->setBufferSeconds(0.25);
        player->update(engine->GetRenderer(), 0.0);
        player->setBufferSeconds(SchoolDaysMediaCacheSeconds);
        pending->second.player = std::move(player);
        pending->second.loopInput = std::move(result.loopInput);
        preparedMedia[event] = std::move(pending->second);
        traceDev("MediaPrepareEnd:Media");
      } catch (const std::exception &error) {
        SDL_LogWarn(Kotonoha::LogUI, "School Days media adoption: %s",
                    error.what());
        mediaPreparationFailed.insert(event);
      }
      pendingMedia.erase(pending);
      continue;
    }
    if (event->command != PLAY_VOICE)
      continue;
    auto [found, inserted] =
        abcActivityCache.emplace(event, std::move(result.activity));
    (void)inserted;
    for (auto &voice : abcVoices)
      if (voice.event == event) {
        voice.activity = found->second;
        voice.cursor = {};
        Kotonoha::SchoolDaysUpdateAbcAnimation(voice.cursor, voice.activity,
                                               event->startTick, tick, speed);
      }
  }
}

bool Runtime::waitForDueMedia(Kotonoha_SceneTick candidateTick) {
  size_t ordinal = 0;
  for (auto *event = script.data; event; event = event->next, ++ordinal) {
    if (event->eventTouched || event->startTick > candidateTick)
      continue;
    if (pendingMedia.count(ordinal) != 0 ||
        pendingBackgrounds.count(ordinal) != 0)
      return true;
    if (event->eventPrepared)
      continue;
    switch (event->command) {
    case CREATE_BG:
    case PLAY_VOICE:
    case PLAY_SE:
    case PLAY_BGM:
    case END_BGM:
    case PLAY_MOVIE:
    case END_ROLL:
      (void)prepareMediaEvent(*event);
      if (pendingMedia.count(ordinal) != 0 ||
          pendingBackgrounds.count(ordinal) != 0)
        return true;
      break;
    default:
      break;
    }
  }
  return false;
}

bool Runtime::prepareMediaEvent(Kotonoha_orsEvent &event) {
  if (!engine || event.eventPrepared)
    return false;

  std::string logical;
  std::string channel;
  std::string loop;
  bool movie = false;
  switch (event.command) {
  case CREATE_BG: {
    size_t ordinal = 0;
    auto *candidate = script.data;
    while (candidate && candidate != &event) {
      candidate = candidate->next;
      ++ordinal;
    }
    if (!candidate)
      return false;
    if (!abcAnalysisWorker)
      abcAnalysisWorker =
          std::make_unique<AbcAnalysisWorker>(*assets, abcAnalysisGeneration);
    std::vector<std::string> abcKeys;
    for (auto *voice = script.data; voice; voice = voice->next) {
      if (voice->command != PLAY_VOICE || voice->startTick >= event.endTick ||
          voice->endTick <= event.startTick)
        continue;
      const auto key = Kotonoha::SchoolDaysNormalizeAbcAnimationKey(
          Kotonoha::SchoolDaysVoiceAnimationKey(voice));
      if (!key.empty() &&
          std::find(abcKeys.begin(), abcKeys.end(), key) == abcKeys.end())
        abcKeys.push_back(key);
    }
    if (!abcAnalysisWorker->enqueueBackground(abcAnalysisGeneration, ordinal,
                                              Text(event.data.create_bg->path),
                                              std::move(abcKeys)))
      return false;
    pendingBackgrounds.insert(ordinal);
    event.eventPrepared = true;
    traceDev("MediaPrepareBegin:Background");
    return true;
  }
  case PLAY_VOICE:
    if (!Kotonoha::SchoolDaysVoiceAllowed(event.data.play_voice->a,
                                          config.menVoice)) {
      event.eventPrepared = true;
      return true;
    }
    logical = std::string(Text(event.data.play_voice->path)) + ".OGG";
    channel = "Voice";
    break;
  case PLAY_SE:
    if (!Kotonoha::SchoolDaysSeSlots::Valid(event.data.play_se->a)) {
      event.eventPrepared = true;
      return true;
    }
    logical = std::string(Text(event.data.play_se->path)) + ".OGG";
    channel = "SE";
    break;
  case PLAY_BGM: {
    const auto resources =
        BgmResourcesForPlayback(*assets, Text(event.data.path_end->path));
    logical = resources.intro;
    loop = resources.loop;
    channel = "BGM";
    break;
  }
  case END_BGM:
    logical = std::string(Text(event.data.path_end->path)) + ".ogg";
    channel = "BGM";
    break;
  case PLAY_MOVIE:
  case END_ROLL: {
    const bool endRoll = event.command == END_ROLL;
    const char *path = endRoll ? Text(event.data.path_end->path)
                               : Text(event.data.play_movie->path);
    const auto state = Kotonoha::SchoolDaysMovieEventState::Create(
        endRoll ? Kotonoha::SchoolDaysMovieKind::EndRoll
                : Kotonoha::SchoolDaysMovieKind::PlayMovie,
        path, event.startTick, event.endTick,
        endRoll ? 0 : event.data.play_movie->a);
    if (!state.Supported()) {
      event.eventPrepared = true;
      return true;
    }
    logical = state.resource;
    channel = "Movie";
    movie = true;
    break;
  }
  default:
    return false;
  }

  event.eventPrepared = true;
  try {
    size_t ordinal = 0;
    auto *candidate = script.data;
    while (candidate && candidate != &event) {
      candidate = candidate->next;
      ++ordinal;
    }
    if (!candidate)
      return true;
    if (!abcAnalysisWorker)
      abcAnalysisWorker =
          std::make_unique<AbcAnalysisWorker>(*assets, abcAnalysisGeneration);
    if (!abcAnalysisWorker->enqueueMedia(abcAnalysisGeneration, ordinal,
                                         logical, !movie, loop)) {
      // A full queue is retried on the next update, without marking this ORS
      // event ready or touching its semantic START.
      event.eventPrepared = false;
      return false;
    }
    traceDev("MediaPrepareBegin:Media");
    PreparedMedia prepared;
    prepared.logical = logical;
    prepared.channel = channel;
    prepared.loop = loop;
    prepared.movie = movie;
    pendingMedia[ordinal] = std::move(prepared);
    if (event.command == PLAY_VOICE)
      prepareAbcActivity(event, logical);
  } catch (const std::exception &error) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "School Days media prepare failed for %s: %s", logical.c_str(),
                error.what());
    mediaPreparationFailed.insert(&event);
  }
  return true;
}

void Runtime::warmOneFutureMedia() {
  if (!engine || !script.data)
    return;
  constexpr Kotonoha_SceneTick kPrepareHorizonTicks = 10u * 24u;
  Kotonoha_SceneTick limit = tick;
  if (std::numeric_limits<Kotonoha_SceneTick>::max() - tick <
      kPrepareHorizonTicks)
    limit = std::numeric_limits<Kotonoha_SceneTick>::max();
  else
    limit = tick + kPrepareHorizonTicks;
  if (const auto next = timeline.NextTick(); next && *next < limit)
    limit = *next;

  for (auto *event = script.data; event != nullptr; event = event->next) {
    if (event->eventTouched || event->eventPrepared ||
        event->startTick < tick || event->startTick > limit)
      continue;
    switch (event->command) {
    case CREATE_BG:
    case PLAY_VOICE:
    case PLAY_SE:
    case PLAY_BGM:
    case END_BGM:
    case PLAY_MOVIE:
    case END_ROLL:
      (void)prepareMediaEvent(*event);
      return; // Old worker deliberately warmed at most one future event/pass.
    default:
      break;
    }
  }
}

void Runtime::discoverAbcForBackground() {
  abcGroups.clear();
  abcTextures.clear();
  abcDecodedSurfaces.clear();
  if (background.empty()) {
    for (auto &voice : abcVoices)
      voice.groupIndex = -1;
    return;
  }

  const size_t separator = background.find_last_of("/\\");
  std::string directory = separator == std::string::npos
                              ? std::string()
                              : background.substr(0, separator + 1u);
  std::vector<std::string> siblings;
  try {
    // School Days GPK references and extracted absolute paths must retain
    // every sibling. The generic overlay list normalizes both to an empty key.
    siblings = assets->listAsset(directory);
  } catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days ABC sibling listing failed: %s",
                error.what());
  }
  abcGroups = Kotonoha::SchoolDaysGroupAbcSiblings(background, siblings);
  abcGroups.erase(
      std::remove_if(abcGroups.begin(), abcGroups.end(),
                     [](const auto &group) { return group.collision; }),
      abcGroups.end());
  rebindActiveAbcVoices();
}

void Runtime::bindAbcVoice(AbcVoiceState &voice) {
  voice.groupIndex = -1;
  voice.cursor = {};
  if (voice.key.empty() || tick < voice.event->startTick ||
      tick >= voice.event->endTick || tick < backgroundStartTick ||
      tick >= backgroundEndTick)
    return;

  // The latest started voice owns a character/ABC key. This matches the old
  // Image binding path, which explicitly unbound the previous same-key group.
  for (auto &other : abcVoices)
    if (&other != &voice && other.key == voice.key)
      other.groupIndex = -1;

  for (size_t index = 0; index < abcGroups.size(); ++index) {
    if (abcGroups[index].key == voice.key) {
      voice.groupIndex = static_cast<int>(index);
      traceDev("AbcBind");
      return;
    }
  }
}

void Runtime::rebindActiveAbcVoices() {
  for (auto &voice : abcVoices)
    voice.groupIndex = -1;
  for (auto &voice : abcVoices)
    if (voice.event && tick >= voice.event->startTick &&
        tick < voice.event->endTick)
      bindAbcVoice(voice);
}

void Runtime::updateAbcVoices() {
  abcVoices.erase(std::remove_if(abcVoices.begin(), abcVoices.end(),
                                 [&](const auto &voice) {
                                   return !voice.event ||
                                          tick >= voice.event->endTick;
                                 }),
                  abcVoices.end());
  for (auto &voice : abcVoices) {
    if (voice.groupIndex < 0 ||
        voice.groupIndex >= static_cast<int>(abcGroups.size()))
      continue;
    Kotonoha::SchoolDaysUpdateAbcAnimation(voice.cursor, voice.activity,
                                           voice.event->startTick, tick, speed);
  }
}

SDL_Texture &Runtime::abcTexture(Kotonoha::RendererRef renderer,
                                 const std::string &resource) {
  if (auto found = abcTextures.find(resource); found != abcTextures.end())
    return *found->second;

  Kotonoha::SurfacePtr decoded;
  bool alreadyPrepared = false;
  if (const auto staged = abcDecodedSurfaces.find(resource);
      staged != abcDecodedSurfaces.end()) {
    decoded = std::move(staged->second);
    abcDecodedSurfaces.erase(staged);
    alreadyPrepared = true;
  } else {
    // Seek reconstruction can still reach this path without a prepared BG.
    decoded = assets->loadSurface(resource);
  }
  if (!decoded)
    throw std::runtime_error("School Days ABC image decode failed");
  Kotonoha::SurfacePtr rgba = alreadyPrepared
                                  ? std::move(decoded)
                                  : Kotonoha::SurfacePtr(SDL_ConvertSurface(
                                        decoded.get(), SDL_PIXELFORMAT_RGBA32));
  if (!rgba)
    throw std::runtime_error(SDL_GetError());
  if (!alreadyPrepared)
    (void)Kotonoha::prepareStraightAlphaForLinearFiltering(
        static_cast<std::uint8_t *>(rgba->pixels), rgba->w, rgba->h,
        rgba->pitch);
  Kotonoha::TexturePtr texture(
      SDL_CreateTextureFromSurface(&renderer, rgba.get()));
  if (!texture ||
      !SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND) ||
      !SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR))
    throw std::runtime_error(SDL_GetError());
  auto [inserted, ok] = abcTextures.emplace(resource, std::move(texture));
  (void)ok;
  return *inserted->second;
}

void Runtime::renderAbcOverlays(Kotonoha::RendererRef renderer,
                                const SDL_FRect &area) {
  for (const auto &voice : abcVoices) {
    if (!voice.event || tick >= backgroundEndTick || voice.groupIndex < 0 ||
        voice.groupIndex >= static_cast<int>(abcGroups.size()) ||
        tick < voice.event->startTick || tick >= voice.event->endTick)
      continue;
    const auto &group = abcGroups[static_cast<size_t>(voice.groupIndex)];
    const auto state = std::min<size_t>(voice.cursor.state.activeIndex, 2u);
    const std::string &resource = group.paths[state];
    if (resource.empty())
      continue;
    try {
      (void)SDL_RenderTexture(&renderer, &abcTexture(renderer, resource),
                              nullptr, &area);
    } catch (const std::exception &error) {
      SDL_LogWarn(Kotonoha::LogUI, "School Days ABC render failed: %s",
                  error.what());
    }
  }
}

void Runtime::resetBackgroundComposition() {
  backgroundComposite.reset();
  backgroundCompositeWidth = 0;
  backgroundCompositeHeight = 0;
  backgroundCompositeKey.clear();
  ++backgroundCompositeSerial;
}

bool Runtime::renderBackgroundWithAbc(Kotonoha::RendererRef renderer,
                                      const SDL_FRect &area) {
  // The old Image layer published DRAW_LAST at its authored end, and Canvas
  // retained that base until another image replaced it. The ORS end tick must
  // stop ABC animation, not turn the whole narrative background black.
  if (background.empty() || tick < backgroundStartTick)
    return false;
  SDL_Texture &base = assets->texture(renderer, background);
  float sourceWidth = 0.0f, sourceHeight = 0.0f;
  if (!SDL_GetTextureSize(&base, &sourceWidth, &sourceHeight) ||
      sourceWidth <= 0.0f || sourceHeight <= 0.0f)
    throw std::runtime_error(SDL_GetError());
  const int width = std::max(1, static_cast<int>(std::lround(sourceWidth)));
  const int height = std::max(1, static_cast<int>(std::lround(sourceHeight)));

  std::string contentKey = background;
  for (const auto &voice : abcVoices) {
    if (!voice.event || tick >= backgroundEndTick || voice.groupIndex < 0 ||
        voice.groupIndex >= static_cast<int>(abcGroups.size()) ||
        tick < voice.event->startTick || tick >= voice.event->endTick)
      continue;
    const auto &group = abcGroups[static_cast<size_t>(voice.groupIndex)];
    const auto state = std::min<size_t>(voice.cursor.state.activeIndex, 2u);
    if (!group.paths[state].empty()) {
      contentKey.push_back('|');
      contentKey += group.paths[state];
    }
  }

  if (!backgroundComposite || backgroundCompositeWidth != width ||
      backgroundCompositeHeight != height) {
    backgroundComposite.reset(
        SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                          SDL_TEXTUREACCESS_TARGET, width, height));
    if (!backgroundComposite)
      throw std::runtime_error(SDL_GetError());
    if (!SDL_SetTextureBlendMode(backgroundComposite.get(),
                                 SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureScaleMode(backgroundComposite.get(),
                                 SDL_SCALEMODE_LINEAR))
      throw std::runtime_error(SDL_GetError());
    backgroundCompositeWidth = width;
    backgroundCompositeHeight = height;
    backgroundCompositeKey.clear();
  }

  if (backgroundCompositeKey != contentKey) {
    SDL_Texture *oldTarget = SDL_GetRenderTarget(&renderer);
    SDL_Rect oldViewport{};
    SDL_Rect oldClip{};
    float oldScaleX = 1.0f, oldScaleY = 1.0f;
    int oldLogicalWidth = 0, oldLogicalHeight = 0;
    SDL_RendererLogicalPresentation oldPresentation =
        SDL_LOGICAL_PRESENTATION_DISABLED;
    Uint8 oldR = 0, oldG = 0, oldB = 0, oldA = 0;
    const bool oldClipEnabled = SDL_RenderClipEnabled(&renderer);
    if (!SDL_GetRenderViewport(&renderer, &oldViewport) ||
        !SDL_GetRenderClipRect(&renderer, &oldClip) ||
        !SDL_GetRenderScale(&renderer, &oldScaleX, &oldScaleY) ||
        !SDL_GetRenderLogicalPresentation(
            &renderer, &oldLogicalWidth, &oldLogicalHeight, &oldPresentation) ||
        !SDL_GetRenderDrawColor(&renderer, &oldR, &oldG, &oldB, &oldA))
      throw std::runtime_error(SDL_GetError());

    const auto restore = [&] {
      (void)SDL_SetRenderTarget(&renderer, oldTarget);
      (void)SDL_SetRenderLogicalPresentation(&renderer, oldLogicalWidth,
                                             oldLogicalHeight, oldPresentation);
      (void)SDL_SetRenderViewport(&renderer, &oldViewport);
      (void)SDL_SetRenderClipRect(&renderer,
                                  oldClipEnabled ? &oldClip : nullptr);
      (void)SDL_SetRenderScale(&renderer, oldScaleX, oldScaleY);
      (void)SDL_SetRenderDrawColor(&renderer, oldR, oldG, oldB, oldA);
    };

    if (!SDL_SetRenderTarget(&renderer, backgroundComposite.get()) ||
        !SDL_SetRenderLogicalPresentation(&renderer, width, height,
                                          SDL_LOGICAL_PRESENTATION_STRETCH) ||
        !SDL_SetRenderViewport(&renderer, nullptr) ||
        !SDL_SetRenderClipRect(&renderer, nullptr) ||
        !SDL_SetRenderScale(&renderer, 1.0f, 1.0f) ||
        !SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 0) ||
        !SDL_RenderClear(&renderer) ||
        !SDL_RenderTexture(&renderer, &base, nullptr, nullptr)) {
      const std::string error = SDL_GetError();
      restore();
      throw std::runtime_error(error);
    }
    const SDL_FRect sourceArea{0.0f, 0.0f, static_cast<float>(width),
                               static_cast<float>(height)};
    renderAbcOverlays(renderer, sourceArea);
    restore();
    backgroundCompositeKey = std::move(contentKey);
    ++backgroundCompositeSerial;
  }

  SDL_Texture &presented = engine->GetUpscaler().process(
      *backgroundComposite, backgroundCompositeSerial);
  if (!SDL_RenderTexture(&renderer, &presented, nullptr, &area))
    throw std::runtime_error(SDL_GetError());
  return true;
}

void Runtime::dispatch(Kotonoha_orsEvent &event) {
  const auto stop = [&](Kotonoha_orsType command) {
    playing.erase(std::remove_if(playing.begin(), playing.end(),
                                 [&](const Playing &item) {
                                   return item.event->command == command;
                                 }),
                  playing.end());
  };
  switch (event.command) {
  case CREATE_BG:
    traceDev("CreateBg");
    // A speculative decode/upload failure is not an authoritative missing BG.
    // Retry the ordinary resolver at the semantic CREATE_BG boundary.
    backgroundPreparationFailed.erase(&event);
    try {
      // Publish the new image only after its asset resolves. A missing retail
      // image must leave the previous Canvas-style DRAW_LAST base intact.
      std::string nextBackground;
      if (const auto prepared = preparedBackgrounds.find(&event);
          prepared != preparedBackgrounds.end()) {
        nextBackground = std::move(prepared->second);
        preparedBackgrounds.erase(prepared);
        background = std::move(nextBackground);
        backgroundStartTick = event.startTick;
        backgroundEndTick = event.endTick;
        abcTextures.clear();
        abcDecodedSurfaces.clear();
        if (const auto groups = preparedBackgroundAbc.find(&event);
            groups != preparedBackgroundAbc.end()) {
          abcGroups = std::move(groups->second);
          preparedBackgroundAbc.erase(groups);
          if (const auto surfaces = preparedBackgroundAbcSurfaces.find(&event);
              surfaces != preparedBackgroundAbcSurfaces.end()) {
            for (auto &item : surfaces->second)
              abcDecodedSurfaces.emplace(std::move(item.first),
                                         std::move(item.second));
            preparedBackgroundAbcSurfaces.erase(surfaces);
          }
          rebindActiveAbcVoices();
        } else {
          discoverAbcForBackground();
        }
      } else {
        nextBackground =
            assets->resolve(Text(event.data.create_bg->path), true);
        background = std::move(nextBackground);
        backgroundStartTick = event.startTick;
        backgroundEndTick = event.endTick;
        discoverAbcForBackground();
      }
    } catch (const std::exception &error) {
      setStatus(error.what());
    }
    break;
  case PLAY_MOVIE:
  case END_ROLL: {
    traceDev("PlayMovie");
    const bool endRoll = event.command == END_ROLL;
    const char *logical = endRoll ? Text(event.data.path_end->path)
                                  : Text(event.data.play_movie->path);
    auto state = Kotonoha::SchoolDaysMovieEventState::Create(
        endRoll ? Kotonoha::SchoolDaysMovieKind::EndRoll
                : Kotonoha::SchoolDaysMovieKind::PlayMovie,
        logical, event.startTick, event.endTick,
        endRoll ? 0 : event.data.play_movie->a);
    bool ready = false;
    if (state.Supported()) {
      // A same-tick WMV -> WMV handoff retains the outgoing decoder's terminal
      // texture until the incoming decoder presents its first frame.  Other
      // movie replacements have no authenticated overlap ownership and retire.
      playing.erase(std::remove_if(playing.begin(), playing.end(),
                                   [&](const Playing &item) {
                                     return item.movie &&
                                            item.event != nullptr &&
                                            item.event->endTick !=
                                                event.startTick;
                                   }),
                    playing.end());
      ready = play(event, state.resource, "Movie");
      if (ready && !playing.empty() && playing.back().event == &event)
        playing.back().movie = true;
    } else {
      setStatus("Unsupported PlayMovie numeric mode");
    }
    state.Prepare(ready);
    (void)state.Activate(tick);
    movieStates[&event] = std::move(state);
    break;
  }
  case PLAY_VOICE: {
    traceDev("PlayVoice");
    // Retail MenVoice: numbered (male) voices only play when enabled.
    if (!Kotonoha::SchoolDaysVoiceAllowed(event.data.play_voice->a,
                                          config.menVoice))
      break;
    const std::string logical =
        std::string(Text(event.data.play_voice->path)) + ".OGG";
    if (!play(event, logical, "Voice"))
      break;
    const std::string key = Kotonoha::SchoolDaysNormalizeAbcAnimationKey(
        Kotonoha::SchoolDaysVoiceAnimationKey(&event));
    if (key.empty())
      break;
    prepareAbcActivity(event, logical);
    const auto cache = abcActivityCache.find(&event);
    AbcVoiceState voice;
    voice.event = &event;
    voice.key = key;
    if (cache != abcActivityCache.end())
      voice.activity = cache->second;
    abcVoices.push_back(std::move(voice));
    bindAbcVoice(abcVoices.back());
    break;
  }
  case PLAY_SE:
    if (!Kotonoha::SchoolDaysSeSlots::Valid(event.data.play_se->a)) {
      SDL_Log("[SD-AUDIO] Ignoring out-of-range SE slot %llu",
              static_cast<unsigned long long>(event.data.play_se->a));
      break;
    }
    playing.erase(std::remove_if(playing.begin(), playing.end(),
                                 [&](const Playing &item) {
                                   return item.event->command == PLAY_SE &&
                                          item.event->data.play_se->a ==
                                              event.data.play_se->a;
                                 }),
                  playing.end());
    play(event, std::string(Text(event.data.play_se->path)) + ".OGG", "SE");
    break;
  case PLAY_BGM: {
    stop(PLAY_BGM);
    auto resources =
        BgmResourcesForPlayback(*assets, Text(event.data.path_end->path));
    play(event, resources.intro, "BGM", resources.loop);
    break;
  }
  case END_BGM:
    stop(END_BGM);
    play(event, std::string(Text(event.data.path_end->path)) + ".ogg", "BGM");
    break;
  case MOVE_SOM:
    moveSom.Start(&event);
    break;
  case PRINT_TEXT:
    caption = &event;
    if (!temporaryPlaybackActive()) {
      backlogStore.AppendLogMessage(
          backlogIndex++,
          Kotonoha::Utf8ToUtf16(Text(event.data.print_text->character)),
          Kotonoha::Utf8ToUtf16(Text(event.data.print_text->text)));
      backlogFallback->addEntry(Text(event.data.print_text->character),
                                Text(event.data.print_text->text));
    } else if (playDataPlayback && playDataPlayback->IsOpen() &&
               !systemUi.IsOpen() && !backlogModel.IsOpen() &&
               !(backlogFallback && backlogFallback->isVisible())) {
      playDataBacklogStore.AppendLogMessage(
          playDataBacklogIndex++,
          Kotonoha::Utf8ToUtf16(Text(event.data.print_text->character)),
          Kotonoha::Utf8ToUtf16(Text(event.data.print_text->text)));
    }
    break;
  case SetSELECT: {
    if (committed)
      break;
    configureChoice(event);
    if (playDataPlayback && playDataPlayback->IsOpen()) {
      int promptResult = -2;
      bool choiceCommitted = false;
      Kotonoha_KtrfError error{};
      Check(playDataPlayback->UpdateChoice(true, &promptResult,
                                           &choiceCommitted, &error),
            error);
      if (choiceCommitted) {
        committed = true;
        choicePresentation.result = promptResult;
        armChoiceExit(promptResult);
      }
    }
    break;
  }
  case BLACK_FADE:
  case WHITE_FADE: {
    // The fade drawable's native 24-fps alpha is projected from the touched
    // ORS event in renderGameplay. No duration extension or generic fade is
    // registered; unlike FadeScene, retail never samples fractional ticks.
    break;
  }
  default:
    break;
  }
}

void Runtime::configureChoice(Kotonoha_orsEvent &event) {
  traceDev("ChoiceOpened");
  choice = &event;
  choicePresentation = {};
  Kotonoha::SchoolDaysPromptPolicy policy{};
  policy.enabled = true;
  policy.twoChoiceOrientation =
      Kotonoha::SchoolDaysPromptOrientation::TopBottom;
  policy.startTick = event.startTick;
  policy.endTick = event.endTick;
  std::size_t count = 0u;
  if (event.data.set_select != nullptr) {
    for (Uint64 i = 0; i < event.data.set_select->size; ++i) {
      const char *option = event.data.set_select->options[i];
      if (option != nullptr && *option != '\0')
        ++count;
    }
  }
  choiceInteraction.Configure(policy, count);
  choiceInteraction.SetAutoMode(config.autoMode);
  // Original v1.02 0x431740 calls 0x4250B0(0) while SETSELECT is shown:
  // effective +0x538 becomes 1x, while the selected MenuBar index at +0x504
  // remains untouched and is restored after the choice. This also unmutes
  // audio for the choice SE and gives the player its full 48-tick exit tail.
  if (!choiceSpeedOverrideActive) {
    choiceSpeedOverrideActive = true;
    applyEffectivePlaybackRate(1.0);
  }
}

int Runtime::semanticChoiceForSlot(int slot) const {
  if (choice == nullptr || choice->data.set_select == nullptr || slot < 0)
    return slot;
  int visible = 0;
  for (Uint64 i = 0; i < choice->data.set_select->size; ++i) {
    const char *option = choice->data.set_select->options[i];
    if (option == nullptr || *option == '\0')
      continue;
    if (visible == slot)
      return static_cast<int>(i);
    ++visible;
  }
  return -1;
}

void Runtime::armChoiceExit(int selected) {
  if (choicePresentation.exitActive)
    return;
  choicePresentation.result = selected;
  choicePresentation.exitSelected = selected;
  choicePresentation.exitStartTick = tick;
  choicePresentation.exitActive = true;
  choicePresentation.finalFrameRendered = false;
  playSound(selected >= 0 ? Ui::UiSfxCue::Select : Ui::UiSfxCue::Cancel);
}

void Runtime::updateChoiceInteraction() {
  if (choice == nullptr || committed || choicePresentation.exitActive)
    return;
  int result = choicePresentation.result;
  if (!choiceInteraction.AdvanceToTick(tick, &result))
    return;
  const int semantic = result >= 0 ? semanticChoiceForSlot(result) : result;
  choicePresentation.result = semantic;
  choose(semantic);
}

bool Runtime::choiceTailActive() const {
  if (!choicePresentation.exitActive || tick < choicePresentation.exitStartTick)
    return false;
  const auto elapsedTicks = tick - choicePresentation.exitStartTick;
  return elapsedTicks < Kotonoha::SchoolDaysChoiceTextPlan::ExitTailTicks(
                            choicePresentation.exitSelected >= 0);
}

bool Runtime::choiceNeedsFinalFrame() const {
  if (!choicePresentation.exitActive || choicePresentation.finalFrameRendered ||
      tick < choicePresentation.exitStartTick)
    return false;
  const auto elapsedTicks = tick - choicePresentation.exitStartTick;
  return elapsedTicks >= Kotonoha::SchoolDaysChoiceTextPlan::ExitTailTicks(
                             choicePresentation.exitSelected >= 0);
}

bool Runtime::choicePresentationComplete() const {
  return !choicePresentation.exitActive ||
         choicePresentation.finalFrameRendered;
}

void Runtime::choose(int value) {
  if (committed)
    return;
  traceDev("ChoiceSelected");
  if (playDataPlayback && playDataPlayback->IsOpen()) {
    int promptResult = value;
    bool choiceCommitted = false;
    Kotonoha_KtrfError error{};
    Check(playDataPlayback->UpdateChoice(true, &promptResult, &choiceCommitted,
                                         &error),
          error);
    if (!choiceCommitted)
      return;
    committed = true;
    choicePresentation.result = promptResult;
    armChoiceExit(promptResult);
    return;
  }
  if (replayPlayback && replayPlayback->IsOpen()) {
    replaySelectResult = value;
    committed = true;
    choicePresentation.result = value;
    armChoiceExit(value);
    return;
  }
  Kotonoha_KtrfError error{};
  int accepted = 0;
  Check(bridge.CommitChoice(value, &accepted, &error), error);
  if (!accepted)
    throw std::runtime_error("School Days choice was not accepted");
  committed = true;
  choicePresentation.result = value;
  armChoiceExit(value);
}

void Runtime::advance() {
  pendingSceneAdvance = false;
  traceDev("SceneTransitionBegin");
  prepareCarryFrame();
  retireSceneEventPointers();
  if (replayPlayback && replayPlayback->IsOpen()) {
    bool completed = false;
    Kotonoha_KtrfError error{};
    Check(replayPlayback->AdvanceAfterSceneEnd(replaySelectResult, &completed,
                                               &error),
          error);
    if (completed) {
      cancelPreparedCarryFrame();
      finishTemporaryPlayback(true);
    } else {
      bindScene(replayPlayback->CurrentScene(),
                replayPlayback->CurrentGameplay());
      commitCarryFrame();
    }
    return;
  }
  if (playDataPlayback && playDataPlayback->IsOpen()) {
    bool terminal = false;
    Kotonoha_KtrfError error{};
    Check(playDataPlayback->AdvanceAfterSceneEnd(&terminal, &error), error);
    if (terminal) {
      cancelPreparedCarryFrame();
      finishTemporaryPlayback(true);
    } else {
      Check(playDataPlayback->Session()
                .Persistence()
                .ReplaceCurrentScriptSnapshot(
                    playDataPlayback->CurrentScene().sceneKey,
                    UINT32_C(0x3f800000), playDataPlayback->Session().State(),
                    &error),
            error);
      bindScene(playDataPlayback->CurrentScene(),
                playDataPlayback->CurrentGameplay());
      commitCarryFrame();
    }
    return;
  }
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.Advance(nullptr, &result, &error), error);
  using Kind = Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind;
  if (result.kind == Kind::BlockedChoice) {
    cancelPreparedCarryFrame();
    choose(-1);
    advance();
  } else if (result.kind == Kind::SwappedScene) {
    openScene(result.scene);
    commitCarryFrame();
  } else if (result.kind == Kind::PendingHandoff ||
             result.kind == Kind::Terminal) {
    // SaveFeel and its nested SaveLoad run before ContinueHandoff. The owned
    // outgoing texture remains available behind these modal screens.
    armHandoff();
  } else if (result.kind == Kind::Unresolved) {
    cancelPreparedCarryFrame();
    paused = true;
    setStatus("Unresolved School Days routing transition: " + scene.sceneKey);
  }
  flushGlobal();
}

void Runtime::armHandoff() {
  if (!bridge.HasPendingHandoff())
    throw std::runtime_error(
        "School Days handoff was armed without a pending bridge transition");
  if (handoffUiState == HandoffUiState::Idle)
    handoffUiState = HandoffUiState::Deferred;
}

void Runtime::updateHandoffUi() {
  if (handoffUiState != HandoffUiState::Deferred)
    return;
  if (!bridge.HasPendingHandoff()) {
    handoffUiState = HandoffUiState::Idle;
    return;
  }
  std::string failure;
  if (!systemUi.OpenSaveFeel(&engine->GetRenderer(), assets->root().string(),
                             uiVariant, &failure))
    throw std::runtime_error("School Days SaveFeel open failed: " + failure);
  handoffUiState = HandoffUiState::SaveFeel;
}

void Runtime::submitSaveFeel(Kotonoha::SchoolDaysSaveFeelModel::Result result) {
  if (handoffUiState != HandoffUiState::SaveFeel || !bridge.HasPendingHandoff())
    return;
  if (result == Kotonoha::SchoolDaysSaveFeelModel::Result::OpenSave) {
    handoffUiState = HandoffUiState::SaveLoad;
    openSaveLoad(true);
    return;
  }
  handoffUiState = HandoffUiState::Continuing;
  closePanels();
  continueHandoff();
}

void Runtime::continueHandoff() {
  if (handoffUiState != HandoffUiState::Continuing ||
      !bridge.HasPendingHandoff())
    return;
  prepareCarryFrame();
  retireSceneEventPointers();
  closePanels();
  const std::string sourceSceneKey = scene.sceneKey;
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.ContinueHandoff(&result, &error), error);
  if (result.kind ==
      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal) {
    cancelPreparedCarryFrame();
    Check(bridge.ReleaseCurrentGameplayForFrontend(&error), error);
    handoffUiState = HandoffUiState::Idle;
    script = {};
    openTitle();
  } else if (result.kind ==
             Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) {
    handoffUiState = HandoffUiState::Idle;
    openScene(result.scene);
    commitCarryFrame();
    if (episodeOverlay.ArmForNaturalHandoff(sourceSceneKey,
                                            result.scene.sceneKey)) {
      const std::string logical =
          Kotonoha::SchoolDaysEpisodeOverlay::TelopLogicalPath(
              episodeOverlay.EpisodeNumber());
      try {
        episodeTelop = assets->resolve(logical);
        SDL_Log("[SD-EPISODE] armed Episode %d scene=%s",
                episodeOverlay.EpisodeNumber(), result.scene.sceneKey.c_str());
      } catch (const std::exception &overlayError) {
        SDL_LogWarn(Kotonoha::LogUI,
                    "School Days Episode telop unavailable: %s",
                    overlayError.what());
        episodeOverlay.Reset();
        episodeTelop.clear();
      }
    }
  } else {
    cancelPreparedCarryFrame();
    throw std::runtime_error(
        "School Days handoff continuation returned an invalid bridge result");
  }
  flushGlobal();
}

void Runtime::back() {
  if (temporaryPlaybackActive()) {
    seek(0);
    return;
  }
  if (!backAtSceneStart) {
    // Retail MenuBar Back is a two-stage navigation control. The first
    // activation rewinds the current physical script; while that start state
    // remains armed, subsequent activations ask RouteProc for predecessors.
    seek(0);
    backAtSceneStart = true;
    return;
  }
  const bool hasPresentedFrame =
      engine && (engine->GetVisualTransition().hasLastPresented() ||
                 engine->GetVisualTransition().outgoingValid());
  if (hasPresentedFrame && !pendingBackCapture) {
    pendingBackCapture = true;
    return;
  }
  pendingBackCapture = false;
  if (hasPresentedFrame)
    prepareCarryFrame();
  retireSceneEventPointers();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.Back(&result, &error), error);
  if (result.kind ==
      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene) {
    openScene(result.scene);
    commitCarryFrame();
    // A Back swap lands at the beginning of the predecessor. Keep the latch
    // armed so another Back continues walking the authenticated predecessor
    // chain instead of redundantly restarting that just-opened scene.
    backAtSceneStart = true;
  } else {
    cancelPreparedCarryFrame();
    setStatus("Beginning of available route history");
  }
}

bool Runtime::completePendingSeek() {
  if (!pendingSeek)
    return true;
  const Kotonoha_SceneTick target = *pendingSeek;
  Kotonoha_orsEvent *latestBackground = nullptr;
  for (auto *event = script.data; event && event->startTick <= target;
       event = event->next)
    if (event->command == CREATE_BG)
      latestBackground = event;

  bool waiting = false;
  size_t ordinal = 0;
  for (auto *event = script.data; event && event->startTick <= target;
       event = event->next, ++ordinal) {
    if (event != latestBackground &&
        (event->command == CREATE_BG || event->endTick <= target))
      continue;
    switch (event->command) {
    case CREATE_BG:
    case PLAY_VOICE:
    case PLAY_SE:
    case PLAY_BGM:
    case END_BGM:
    case PLAY_MOVIE:
    case END_ROLL:
      if (!event->eventPrepared)
        (void)prepareMediaEvent(*event);
      if (!event->eventPrepared || pendingMedia.count(ordinal) != 0 ||
          pendingBackgrounds.count(ordinal) != 0)
        waiting = true;
      break;
    default:
      break;
    }
  }
  if (waiting)
    return false;

  pendingSeek.reset();
  playing.clear();
  movieStates.clear();
  moveSom.Reset();
  caption = choice = nullptr;
  choicePresentation = {};
  choiceInteraction.ResetSelection();
  choiceInteraction.SetAutoMode(config.autoMode);
  background.clear();
  backgroundStartTick = backgroundEndTick = 0;
  abcGroups.clear();
  abcVoices.clear();
  abcTextures.clear();
  abcDecodedSurfaces.clear();
  resetBackgroundComposition();
  timeline.Restart();
  elapsed = target / 24.0;
  tick = target;
  for (auto *event : timeline.AdvanceTo(target))
    if (event == latestBackground ||
        (event->command != CREATE_BG && event->endTick > target))
      dispatch(*event);
  lastClock = SDL_GetTicks();
  return true;
}

void Runtime::seek(Kotonoha_SceneTick target) {
  if (choiceSpeedOverrideActive) {
    choiceSpeedOverrideActive = false;
    setPlaybackSpeedIndex(speedIndex);
  }
  pendingSceneAdvance = false;
  pendingBackCapture = false;
  // An input-driven seek runs between presented frames. Keep the latest
  // pre-Present capture until the destination has a real BG or movie frame.
  commitCarryFrame();
  resetPreparedMedia();
  for (auto *event = script.data; event != nullptr; event = event->next)
    event->eventPrepared = false;
  backAtSceneStart = false;
  abcActivityCache.clear();
  pendingSeek = target;
  for (auto &item : playing)
    item.player->setPaused(true);
}

void Runtime::skipToNextChoice() {
  if (temporaryPlaybackActive()) {
    setStatus("Next-choice skip is unavailable during Replay playback");
    return;
  }
  commitCarryFrame();
  playing.clear();
  backAtSceneStart = false;
  // Mode 5 may replace several Gameplay/ORS owners before it returns.
  retireSceneEventPointers();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.AdvanceToNextChoice("ktrf:next", config.skip, &result, &error),
        error);
  using Kind = Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind;
  switch (result.kind) {
  case Kind::BlockedChoice: {
    // AdvanceToNextChoice may have traversed several already-read physical
    // scripts before the router reaches the choice. Bind the final Gameplay
    // first, then apply the authored SkipFRAME boundary atomically so no tick-0
    // frame from the destination leaks to presentation.
    if (scene.sceneKey != bridge.CurrentScene().sceneKey)
      openScene(bridge.CurrentScene());
    const auto target = timeline.ComputeSkipToEndTarget(tick);
    if (!target)
      throw std::runtime_error(
          "School Days next-choice destination has no skip target");
    seek(*target);
    break;
  }
  case Kind::SwappedScene:
    // Config Skip is off and the destination is unread. Native mode5 stops at
    // that script's beginning and leaves ordinary playback to continue there.
    openScene(result.scene);
    break;
  case Kind::PendingHandoff:
  case Kind::Terminal:
    armHandoff();
    break;
  case Kind::Unresolved:
    paused = true;
    setStatus("Unresolved School Days next-choice transition: " +
              scene.sceneKey);
    break;
  case Kind::BackBoundary:
  case Kind::BackTrialPv:
  case Kind::BackNoScript:
    throw std::runtime_error("School Days next-choice returned a Back result");
  }
  flushGlobal();
}

void Runtime::update() {
  const Uint64 now = SDL_GetTicks();
  const double delta = (now - lastClock) / 1000.0;
  lastClock = now;
  if (currentState == Kotonoha::GameState::Splash) {
    const auto active = GetActiveScenes();
    for (auto *scene : active)
      scene->update();
    const bool logo = std::find(active.begin(), active.end(),
                                logoSplash.get()) != active.end();
    if (!logo && engineSplash && engineSplash->finished()) {
      RemoveScene(engineSplash.get());
      // The logo's three-second envelope begins when its stage becomes
      // active, after the preceding engine splash has completed.
      logoSplash->restart();
      AddScene(logoSplash.get(), Kotonoha::GameState::Splash);
    } else if (logo && logoSplash->finished()) {
      finishSplash();
    }
    return;
  }
  // Frontend handoffs occur exactly at fade opacity, before retained scenes or
  // panel models get another update in the old ownership state.
  updateFrontendTransition(now);
  updateExitTransition(now);
  updateSystemPanelTransition(now);
  updateLoadTransition(now);
  updateTransportFade(now);
  gameScene->update();
  if (backlogFallback->isVisible())
    backlogFallback->update();
  updateHandoffUi();
  if (systemUi.IsOpen()) {
    if (systemUi.IsSaveLoadOpen() && saveLoadPage != systemUi.SaveLoad().Page())
      refreshSaveMetadata();
    if (systemUi.IsRouteMapOpen() &&
        (routeMapMajor != systemUi.RouteMap().Major() ||
         routeMapSubpage != systemUi.RouteMap().Subpage()))
      refreshRouteMap();
    int64_t feeling001 = 0;
    int64_t feeling002 = 0;
    if (handoffUiState == HandoffUiState::SaveFeel) {
      Kotonoha_KtrfError error{};
      Check(bridge.Session().State().ReadSessionInt("001", &feeling001, &error),
            error);
      Check(bridge.Session().State().ReadSessionInt("002", &feeling002, &error),
            error);
    }
    Ui::Output output;
    systemUi.Update(static_cast<uint32_t>(now), feeling001, feeling002,
                    &output);
    handlePanel(output);
  }
  sounds.erase(
      std::remove_if(sounds.begin(), sounds.end(),
                     [](const auto &sound) { return sound->finished(); }),
      sounds.end());
  const bool stopped = modal();
  if (titleBgm)
    titleBgm->setPaused(stopped && currentState != Kotonoha::GameState::Menu);
  if (currentState != Kotonoha::GameState::Gameplay)
    return;
  using TransportPhase = Kotonoha::SchoolDaysTransportFade::Phase;
  if (transportFade.CurrentPhase() == TransportPhase::FadeOut ||
      transportFade.CurrentPhase() == TransportPhase::AwaitNavigation) {
    for (auto &item : playing)
      item.player->setPaused(true);
    return;
  }
  if (pendingBackCapture) {
    if (engine && (engine->GetVisualTransition().hasLastPresented() ||
                   engine->GetVisualTransition().outgoingValid()))
      back();
    return;
  }
  if (pendingSceneAdvance) {
    advance();
    return;
  }
  pollAbcActivity();
  if (pendingSeek) {
    for (auto &item : playing)
      item.player->setPaused(true);
    if (completePendingSeek()) {
      if (transportFade.CurrentPhase() == TransportPhase::AwaitReady)
        transportFade.Reveal(now);
      if (paused || modal())
        for (auto &item : playing)
          item.player->setPaused(true);
    }
    return;
  }
  if (transportFade.CurrentPhase() == TransportPhase::AwaitReady)
    transportFade.Reveal(now);
  for (auto &voice : abcVoices)
    if (voice.event && abcActivityCache.count(voice.event) == 0)
      prepareAbcActivity(*voice.event,
                         std::string(Text(voice.event->data.play_voice->path)) +
                             ".OGG");
  // Commercial EventRuntime warmed one future media event at a time inside a
  // 10-second window, capped at Next. Do this before semantic dispatch so a
  // boundary event can adopt an already-open decoder instead of opening FFmpeg
  // on the authored START tick.
  warmOneFutureMedia();
  const bool halted =
      paused || stopped ||
      (!temporaryPlaybackActive() && bridge.HasPendingHandoff());
  for (auto &item : playing)
    item.player->setPaused(halted);
  if (halted)
    return;
  const double candidateElapsed = elapsed + delta * speed;
  auto candidateTick = static_cast<Kotonoha_SceneTick>(candidateElapsed * 24);
  bool clampedToChoice = false;
  // The original 0x431740/0x4250B0 lowers the effective narrative clock to
  // 1x when SETSELECT is entered. Fast-forward must never jump beyond the
  // authored choice start during a long frame: stop the accelerated clock
  // exactly on that pending event, dispatch the prompt, then advance at 1x.
  if (!choiceSpeedOverrideActive && speed > 1.0) {
    for (auto *event = script.data; event != nullptr; event = event->next) {
      if (event->startTick > candidateTick)
        break;
      if (event->command == SetSELECT && !event->eventTouched &&
          event->startTick >= tick) {
        candidateTick = event->startTick;
        clampedToChoice = true;
        break;
      }
    }
  }
  if (waitForDueMedia(candidateTick))
    return;
  // An authored choice boundary may have truncated the accelerated frame.
  // Match the exact timeline tick instead of using the discarded overshoot.
  elapsed = clampedToChoice ? static_cast<double>(candidateTick) / 24.0
                            : candidateElapsed;
  tick = candidateTick;
  for (auto *event : timeline.AdvanceTo(tick))
    dispatch(*event);
  updateAbcVoices();
  moveSom.AdvanceTo(tick);
  for (auto &[event, state] : movieStates) {
    (void)event;
    if (tick >= state.endTick)
      (void)state.AdvanceTo(tick);
  }
  updateChoiceInteraction();
  if (timeline.ReadyToEnd(tick) && choicePresentationComplete())
    pendingSceneAdvance = true;
}

void Runtime::renderMenu(Kotonoha::RendererRef renderer, float) {
  const auto titleLayout =
      Kotonoha::SchoolDaysTitleRenderPlan::LayoutFor(uiVariant);
  const auto titleViewport =
      FitToRuntime(titleLayout.virtualWidth, titleLayout.virtualHeight);
  const auto titleRect = [&](const SDL_FRect &native) {
    return TransformToRuntime(native, titleViewport);
  };
  if (titleMovie) {
    try {
      const SDL_FRect movieArea =
          titleRect({0.0f, titleLayout.yOffset * titleLayout.scale,
                     800.0f * titleLayout.scale, 452.0f * titleLayout.scale});
      const Uint64 elapsed = SDL_GetTicks() - titleMovieStartMs;
      const Uint64 frame =
          Kotonoha::SchoolDaysTitleMovieClock::FrameIndex(elapsed);
      const double target =
          Kotonoha::SchoolDaysTitleMovieClock::TargetMilliseconds(frame) /
          1000.0;
      titleMovie->update(renderer, target);
      titleMovie->render(renderer, movieArea, nullptr, true);
    } catch (const std::exception &movieError) {
      SDL_LogWarn(Kotonoha::LogUI,
                  "School Days Title EndBG movie decode failed; using BaseFile "
                  "fallback: %s",
                  movieError.what());
      titleMovie.reset();
      titleMovieStartMs = 0;
      titleBackgroundHeight = 450.0f;
    }
  }
  if (!titleMovie && !titleBackground.empty()) {
    const SDL_FRect area =
        titleRect({0.0f, titleLayout.yOffset * titleLayout.scale,
                   800.0f * titleLayout.scale,
                   titleBackgroundHeight * titleLayout.scale});
    try {
      SDL_RenderTexture(&renderer, &assets->texture(renderer, titleBackground),
                        nullptr, &area);
    } catch (const std::exception &) {
      titleBackground.clear();
    }
  }
  if (titleBgm) {
    titleBgm->update(renderer);
    if (titleBgm->finished()) {
      const std::string &stem = frontendBgmStem();
      if (!stem.empty())
        titleBgm->open(*assets, assets->resolve(stem + "_loop.ogg"), true,
                       "BGM");
    }
  }
  // ExitView is a translucent confirmation over the title, so retain the
  // title overlay beneath it. Other frontend panels replace the title.
  if ((!systemUi.IsOpen() || systemUi.IsExitOpen()) && titleArtwork)
    try {
      for (const auto &sprite :
           Kotonoha::SchoolDaysTitleRenderPlan::Build(title)) {
        const auto stem =
            sprite.texture ==
                    Kotonoha::SchoolDaysTitleRenderPlan::Texture::Overlay
                ? Kotonoha::SchoolDaysTitleModel::ResourceStem(
                      title.CurrentFamily())
                : Kotonoha::SchoolDaysTitleModel::ChipStem(
                      title.CurrentFamily());
        const auto path = assets->resolve(stem, true);
        const SDL_FRect destination =
            titleRect({sprite.destination.x, sprite.destination.y,
                       sprite.destination.w, sprite.destination.h});
        const SDL_FRect source{sprite.source.x, sprite.source.y,
                               sprite.source.w, sprite.source.h};
        SDL_RenderTexture(&renderer, &assets->texture(renderer, path),
                          sprite.hasSource ? &source : nullptr, &destination);
      }
    } catch (const std::exception &error) {
      SDL_LogWarn(Kotonoha::LogUI, "School Days title: %s", error.what());
      titleArtwork = false;
    }
  if (!titleArtwork && !panelOpen()) {
    auto &ui = engine->GetUI();
    ui.panel(renderer, {360, 140, 560, 440});
    ui.text(renderer, "School Days HQ", 400, 175, 480);
    constexpr const char *labels[]{"New game", "Load game", "Configuration",
                                   "Quit"};
    for (int i = 0; i < 4; ++i)
      ui.button(renderer, {400, 250.0f + i * 70, 480, 55}, labels[i],
                titleSelection == i);
  }
  renderPanels(renderer);
  if (!replayHSceneTransitionActive)
    renderFrontendTransition(renderer, SDL_GetTicks());
}

void Runtime::renderGameplay(Kotonoha::RendererRef renderer, float) {
  const SDL_FRect area{0, 0, 1280, 720};
  bool primaryReady = false;
  framePrimaryReady = false;
  frameBoundary = false;
  if (!background.empty()) {
    try {
      if (renderBackgroundWithAbc(renderer, area))
        primaryReady = true;
    } catch (const std::exception &error) {
      setStatus(error.what());
      background.clear();
    }
  }
  const bool halted =
      pendingSeek.has_value() || paused || modal() ||
      (!temporaryPlaybackActive() && bridge.HasPendingHandoff());
  for (auto it = playing.begin(); it != playing.end();) {
    if (!it->movie && tick >= it->event->endTick) {
      it = playing.erase(it);
      continue;
    }
    if (it->movie && tick >= it->event->endTick) {
      const auto adjacent = std::find_if(
          playing.begin(), playing.end(), [&](const Playing &candidate) {
            return &candidate != &*it && candidate.movie &&
                   candidate.event != nullptr &&
                   candidate.event->startTick == it->event->endTick &&
                   tick < candidate.event->endTick;
          });
      const bool incomingReady =
          adjacent != playing.end() && adjacent->player->hasVideoFrame();
      if (it->terminalFramePresented &&
          (adjacent == playing.end() || incomingReady)) {
        it = playing.erase(it);
        continue;
      }
      try {
        // MediaPlayer intentionally keeps its last uploaded texture at EOF. Do
        // not advance past the authored END; publish that terminal frame once,
        // or keep it while an adjacent decoder is still waiting for frame 0.
        const bool hadVideoFrame = it->player->hasVideoFrame();
        // Retail movies cover the authored 1280x720 canvas. Letterboxing a
        // slightly different WMV sample aspect exposes the previous scene at
        // the edges, including while the terminal movie frame is retained.
        it->player->render(renderer, area, &engine->GetUpscaler(), true);
        if (hadVideoFrame)
          primaryReady = true;
        it->terminalFramePresented = true;
      } catch (const std::exception &error) {
        setStatus(error.what());
        it->player->stop();
      }
      ++it;
      continue;
    }
    auto &item = *it++;
    try {
      if (!halted)
        item.player->update(renderer,
                            std::max(0.0, elapsed - item.segmentStart));
      if (item.movie && item.player->finished()) {
        auto state = movieStates.find(item.event);
        if (state != movieStates.end() && !state->second.eof)
          state->second.MarkEof();
      }
      const bool hadVideoFrame = item.movie && item.player->hasVideoFrame();
      item.player->render(renderer, area,
                          item.movie ? &engine->GetUpscaler() : nullptr,
                          item.movie);
      if (hadVideoFrame)
        primaryReady = true;
      if (!item.loop.empty() && item.player->finished()) {
        const auto channel = item.player->getChannel();
        if (item.preparedLoop) {
          item.player->adoptPreparedInput(std::move(item.preparedLoop),
                                          channel ? channel->getName() : "BGM");
          item.player->setPaused(false);
        } else {
          item.player->open(*assets, assets->resolve(item.loop), true,
                            channel ? channel->getName() : "BGM");
        }
        item.looping = true;
        item.segmentStart = elapsed;
      }
    } catch (const std::exception &error) {
      setStatus(error.what());
      item.player->stop();
    }
  }
  // Render the ORS fade over background/media, but UNDER the subtitle layer.
  // Script text must remain legible even when a BlackFade or WhiteFade reaches
  // full coverage; this is distinct from a physical frontend/transport fade.
  // Never use elapsed's fractional sub-tick phase here: retail computes
  // alpha=floor((tick-start)*255/(end-start)) at 24 fixed script fps.
  const auto authoredNext = timeline.NextTick();
  const auto authoredFade = Kotonoha::SchoolDaysOrsFadeFrame::AtTick(
      script, tick,
      authoredNext ? std::optional<Kotonoha_SceneTick>{*authoredNext}
                   : std::nullopt,
      pendingSceneAdvance);
  if (authoredFade.drawable && authoredFade.alpha != 0) {
    SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    (void)SDL_GetRenderDrawBlendMode(&renderer, &oldBlend);
    (void)SDL_GetRenderDrawColor(&renderer, &red, &green, &blue, &alpha);
    (void)SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    (void)SDL_SetRenderDrawColor(&renderer, authoredFade.color,
                                 authoredFade.color, authoredFade.color,
                                 authoredFade.alpha);
    (void)SDL_RenderFillRect(&renderer, &area);
    (void)SDL_SetRenderDrawColor(&renderer, red, green, blue, alpha);
    (void)SDL_SetRenderDrawBlendMode(&renderer, oldBlend);
  }
  // TextView=Off still removes the authored subtitle layer entirely.
  // Always compose visible subtitles after the ORS fade, independently of
  // whether the next script is awaiting a deferred handoff.
  if (config.textView) {
    subtitles->setPosition(elapsed);
    renderScene(*subtitles, renderer, 1);
  }
  if (!temporaryPlaybackActive() && episodeOverlay.AppliesTo(scene.sceneKey) &&
      !episodeTelop.empty()) {
    const uint64_t sceneMs =
        static_cast<uint64_t>(std::max(0.0, elapsed) * 1000.0);
    const Uint8 alpha = Kotonoha::SchoolDaysEpisodeOverlay::Alpha(sceneMs);
    if (alpha != 0u) {
      try {
        auto &texture = assets->texture(renderer, episodeTelop);
        SDL_SetTextureAlphaMod(&texture, alpha);
        SDL_RenderTexture(&renderer, &texture, nullptr, &area);
        SDL_SetTextureAlphaMod(&texture, 255);
      } catch (const std::exception &overlayError) {
        SDL_LogWarn(Kotonoha::LogUI,
                    "School Days Episode telop render failed: %s",
                    overlayError.what());
        episodeOverlay.Reset();
        episodeTelop.clear();
      }
    }
  }
  renderScene(*gameScene, renderer, 1);
  // The engine owns the Novel target and composites the outgoing texture when
  // this scene has no drawable primary image/video. Nothing reads a swapchain.
  // Neither entrance nor exit at full opacity is a drawable primary visual.
  // Copying the terminal black/white exit into LastPresented would make the
  // next scene's outgoing lease itself opaque, causing a transition flash.
  const bool visualCoveredByFade =
      authoredFade.drawable && authoredFade.alpha == 255;
  framePrimaryReady =
      primaryReady && !visualCoveredByFade && !pendingSeek && !modal();
  frameBoundary =
      (pendingSceneAdvance || choicePresentation.finalFrameRendered) &&
      !modal();
}

void Runtime::renderNarrativeOverlay(Kotonoha::RendererRef renderer, float) {
  if (currentState != Kotonoha::GameState::Gameplay)
    return;
  renderPanels(renderer);
  if (!replayHSceneTransitionActive)
    renderFrontendTransition(renderer, SDL_GetTicks());
}

void Runtime::renderPanels(Kotonoha::RendererRef renderer) {
  for (auto &sound : sounds)
    sound->update(renderer);
  if (backlogModel.IsOpen()) {
    std::string failure;
    if (!backlogPresenter.Render(&renderer, backlogModel, &failure)) {
      SDL_LogWarn(Kotonoha::LogUI, "School Days BackLog: %s", failure.c_str());
      const bool privatePlayData = backlogOwner == BackLogOwner::PlayData;
      backlogModel.Close();
      backlogPresenter.Close();
      backlogOwner = BackLogOwner::None;
      if (privatePlayData)
        setStatus("Replay Data BackLog render failed: " + failure);
      else {
        backlogOwner = BackLogOwner::Narrative;
        backlogFallback->open();
      }
    }
  }
  if (saveMenu->isOpen())
    renderScene(*saveMenu, renderer, 1);
  if (backlogFallback->isVisible())
    renderScene(*backlogFallback, renderer, 1);
  renderStatus(renderer);
}

void Runtime::renderScreen(Kotonoha::RendererRef renderer, float scale) {
  if (!engine)
    return;
  // The engine splash and retail logo are Screen scenes. The School Days
  // overlay must compose them before its physical UI instead of replacing
  // the generic Screen scene pass.
  Kotonoha::Runtime::renderScreen(renderer, scale);
  // A native SystemUi or BackLog panel owns the screen while open. The menu
  // belongs to gameplay and must not be projected over that owner.
  if (currentState == Kotonoha::GameState::Gameplay && gameScene &&
      !panelOpen() && !exitTransition.Active())
    gameScene->renderMenuBar(renderer);
  if (systemUi.IsOpen()) {
    const SDL_FPoint screen = engine->screenSize();
    systemUi.SetArea({std::max(1, static_cast<int>(std::lround(screen.x))),
                      std::max(1, static_cast<int>(std::lround(screen.y)))});
    const bool standardFullConfig =
        systemUi.IsConfigOpen() && narrativePresentationHeightScale() < 0.999f;
    systemUi.SetConfigStandardFullscreenStretch(standardFullConfig);
    if (standardFullConfig) {
      // The Standard authored 800x450 option art leaves transparent physical
      // 75px bands. Clear the old story beneath the whole output; a black
      // backdrop and stretched native 800x600 host frame reproduce the image.
      Uint8 red = 0, green = 0, blue = 0, alpha = 0;
      (void)SDL_GetRenderDrawColor(&renderer, &red, &green, &blue, &alpha);
      (void)SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
      (void)SDL_RenderFillRect(&renderer, nullptr);
      (void)SDL_SetRenderDrawColor(&renderer, red, green, blue, alpha);
    }
    std::string failure;
    if (!systemUi.Render(&renderer, &failure)) {
      SDL_LogWarn(Kotonoha::LogUI, "School Days panel: %s", failure.c_str());
      systemUi.Close();
      setStatus("School Days panel failed: " + failure);
      return;
    }
    // The desktop Display base+CHIP are rendered by the native CMAP presenter:
    // only dynamic values and modal selectors belong to this overlay. Android
    // deliberately retains its historical overlay on all Config pages.
#if defined(__ANDROID__) || defined(ANDROID)
    if (systemUi.IsConfigOpen())
      renderDisplayPanel(renderer);
#else
    if (displayPanelOpen())
      renderDisplayPanel(renderer);
#endif
  }
  // The H-scene handoff covers the physical Replay gallery on the outgoing
  // phase and the entire presentation on the incoming phase. A fade drawn
  // only in renderMenu would otherwise sit UNDER SystemUi's native panel.
  if (replayHSceneTransitionActive &&
      frontendTransition.Active(SDL_GetTicks())) {
    SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
    Uint8 r = 0, g = 0, b = 0, a = 0;
    (void)SDL_GetRenderDrawBlendMode(&renderer, &oldBlend);
    (void)SDL_GetRenderDrawColor(&renderer, &r, &g, &b, &a);
    (void)SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
    (void)SDL_SetRenderDrawColor(&renderer, 0, 0, 0,
                                 frontendTransition.Alpha(SDL_GetTicks()));
    (void)SDL_RenderFillRect(&renderer, nullptr);
    (void)SDL_SetRenderDrawColor(&renderer, r, g, b, a);
    (void)SDL_SetRenderDrawBlendMode(&renderer, oldBlend);
  }
  renderExitTransition(renderer, SDL_GetTicks());
  renderSystemPanelTransition(renderer, SDL_GetTicks());
  renderLoadTransition(renderer, SDL_GetTicks());
  renderTransportFade(renderer, SDL_GetTicks());
}

#if !defined(__ANDROID__) && !defined(ANDROID)
void Runtime::discardDesktopDisplayDraft() {
  if (desktopDisplayPopup == DesktopDisplayPopup::GlslPath && engine)
    SDL_StopTextInput(&engine->GetWindow());
  desktopDisplayPopup = DesktopDisplayPopup::None;
  desktopDisplayPopupHover = -1;
  desktopDisplayPopupScroll = 0;
  desktopDisplayPathEdit.clear();
  desktopDisplayModes.clear();
  desktopDisplayDraftActive = false;
}

void Runtime::resetDesktopDisplayDraft() {
  discardDesktopDisplayDraft();
  if (!engine)
    return;
  desktopDisplayDraft = engine->GetSettings();
  desktopDisplayDraftActive = true;
}

void Runtime::activateDesktopDisplayControl(int control) {
  if (!engine || !desktopDisplayDraftActive || !displayPanelOpen())
    return;
  auto &draft = desktopDisplayDraft;
  const auto openPopup = [&](DesktopDisplayPopup popup, int selected) {
    desktopDisplayPopup = popup;
    desktopDisplayPopupHover = selected;
    desktopDisplayPopupScroll = std::max(0, selected - 3);
  };
  switch (control) {
  case 4: {
    const SDL_DisplayID display = DisplayIdForIndex(draft.display);
    desktopDisplayModes = Kotonoha::DisplayModeCatalog::Enumerate(display);
    if (desktopDisplayModes.empty()) {
      setStatus("No supported resolutions for this monitor");
      return;
    }
    int width = draft.width, height = draft.height;
    float refresh = draft.refreshRate;
    if (draft.mode == Kotonoha::WindowMode::Borderless) {
      if (const auto *native = SDL_GetDesktopDisplayMode(display)) {
        width = native->w;
        height = native->h;
        refresh = native->refresh_rate;
      }
    } else if (draft.mode != Kotonoha::WindowMode::Windowed &&
               draft.fullscreenWidth > 0 && draft.fullscreenHeight > 0) {
      width = draft.fullscreenWidth;
      height = draft.fullscreenHeight;
    }
    openPopup(DesktopDisplayPopup::Resolution,
              Kotonoha::DisplayModeCatalog::FindSelected(
                  desktopDisplayModes, width, height, refresh));
    break;
  }
  case 5:
    draft.resizable = true;
    break;
  case 6:
    draft.resizable = false;
    break;
  case 7:
    draft.vsync = true;
    break;
  case 8:
    draft.vsync = false;
    break;
  case 9: {
    const std::array<Kotonoha::UpscaleBackend, 5> values{
        Kotonoha::UpscaleBackend::None, Kotonoha::UpscaleBackend::Linear,
        Kotonoha::UpscaleBackend::MPV, Kotonoha::UpscaleBackend::MetalFX,
        Kotonoha::UpscaleBackend::Auto};
    const auto it =
        std::find(values.begin(), values.end(), draft.upscaling.backend);
    openPopup(DesktopDisplayPopup::Upscaler,
              it == values.end() ? 4 : static_cast<int>(it - values.begin()));
    break;
  }
  case 10: {
    if (engine->GetUpscaler().resolve(draft.upscaling.backend) !=
        Kotonoha::UpscaleBackend::MPV) {
      setStatus("Shader presets require the MPV/OpenGL upscaler");
      return;
    }
    const auto &presets = Kotonoha::UpscalePresets();
    const auto it =
        std::find(presets.begin(), presets.end(), draft.upscaling.preset);
    openPopup(DesktopDisplayPopup::Shader,
              it == presets.end() ? 0 : static_cast<int>(it - presets.begin()));
    break;
  }
  case 11:
  case 12:
    if (engine->GetUpscaler().resolve(draft.upscaling.backend) !=
        Kotonoha::UpscaleBackend::MPV) {
      setStatus("Custom GLSL requires the MPV/OpenGL upscaler");
      return;
    }
    // BROWSE deliberately opens the keyboard/paste path editor. There is no
    // native file picker in this game-owned runtime, and no global
    // SettingsMenu.
    desktopDisplayPathEdit = draft.upscaling.shaderFiles;
    desktopDisplayPopup = DesktopDisplayPopup::GlslPath;
    desktopDisplayPopupHover = -1;
    SDL_StartTextInput(&engine->GetWindow());
    if (control == 12)
      setStatus("BROWSE: paste a GLSL file path and press Enter");
    break;
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
  case 19:
    draft.upscaling.factor = 1.0f + (control - 13) * 0.5f;
    break;
  case 20:
    // Defaults affect only this page. Never reset the player's General
    // WINDOW/FULL state, aspect policy, monitor, audio or save preferences.
    if (const SDL_DisplayID display = DisplayIdForIndex(draft.display)) {
      if (const SDL_DisplayMode *native = SDL_GetDesktopDisplayMode(display)) {
        // Native monitor resolution is the safe fallback in both modes.
        // Preserve WindowMode itself: only General may change WINDOW/FULL.
        draft.width = native->w;
        draft.height = native->h;
        draft.fullscreenWidth = native->w;
        draft.fullscreenHeight = native->h;
        draft.refreshRate = native->refresh_rate;
      }
    }
    draft.resizable = true;
    draft.vsync = true;
    draft.upscaling.backend =
        engine->GetUpscaler().supports(Kotonoha::UpscaleBackend::Auto)
            ? Kotonoha::UpscaleBackend::Auto
            : Kotonoha::UpscaleBackend::None;
    draft.upscaling.preset = Kotonoha::UpscalePreset::Anime4K;
    draft.upscaling.shaderFiles.clear();
    draft.upscaling.factor = 1.0f;
    break;
  case 21:
    try {
      // This page must never change the native General WINDOW/FULL controls.
      draft.mode = engine->GetSettings().mode;
      draft.display = engine->GetSettings().display;
#if defined(_WIN32)
      // General WINDOW intentionally maximizes the decorated desktop client.
      // A subsequent explicit Windowed resolution choice must first restore
      // that window, or SDL_SetWindowSize changes the hidden restore geometry
      // while the visible client stays maximized at the desktop resolution.
      const auto &live = engine->GetSettings();
      const char *driver = SDL_GetCurrentVideoDriver();
      const bool restoreForSize =
          draft.mode == Kotonoha::WindowMode::Windowed &&
          (draft.width != live.width || draft.height != live.height) &&
          driver && SDL_strcmp(driver, "windows") == 0 &&
          (SDL_GetWindowFlags(&engine->GetWindow()) & SDL_WINDOW_MAXIMIZED) !=
              0;
      if (restoreForSize && !SDL_RestoreWindow(&engine->GetWindow()))
        throw std::runtime_error(std::string("Restore window: ") +
                                 SDL_GetError());
#endif
      try {
        engine->applySettings(
            draft); // validates, applies, and persists settings
      } catch (...) {
#if defined(_WIN32)
        if (restoreForSize)
          SDL_MaximizeWindow(&engine->GetWindow());
#endif
        throw;
      }
      resetDesktopDisplayDraft();
      setStatus("Display Options applied");
    } catch (const std::exception &error) {
      setStatus(std::string("Display Options APPLY failed: ") + error.what());
    }
    break;
  default:
    return;
  }
  if (control != 21) {
    desktopDisplayPopupHover = std::max(-1, desktopDisplayPopupHover);
    systemUi.SetConfigVisualState(configVisualState());
  }
}

void Runtime::renderDesktopDisplayPanel(Kotonoha::RendererRef renderer) {
  if (!displayPanelOpen() || !desktopDisplayDraftActive)
    return;
  auto &ui = engine->GetUI();
  const auto canvas = DesktopCanvas(
      systemUi.ConfigMenu().CurrentVariant(), engine->screenSize(),
      narrativePresentationHeightScale() < 0.999f);
  const SDL_Color ink{30, 43, 66, 255};
  const auto draw = [&](const std::string &value, float x, float y,
                        float width) {
    const auto rect = canvas.map(x, y, width, 24.0f);
    // The user's accepted Display skin centers variable values inside its
    // silver controls. These texts are deliberately absent from the base PNG
    // so the live value can be drawn here without a baked-in placeholder.
    const auto measured = ui.textSize(value, rect.w);
    const float left = rect.x + std::max(0.0f, (rect.w - measured.x) * 0.5f);
    ui.text(renderer, value, left, rect.y, rect.w, ink);
  };
  const auto &draft = desktopDisplayDraft;
  const SDL_DisplayID display = DisplayIdForIndex(draft.display);
  const auto *native = display ? SDL_GetDesktopDisplayMode(display) : nullptr;
  if (draft.mode == Kotonoha::WindowMode::Borderless && native) {
    // Borderless always uses the monitor's actual desktop mode, independently
    // of the fullscreen tuple left from a prior WINDOW or Exclusive session.
    draw(Kotonoha::DisplayModeCatalog::Label(
             {native->w, native->h, native->refresh_rate}) +
             " (current)",
         254.0f, 113.0f, 488.0f);
  } else {
    const int width =
        draft.mode == Kotonoha::WindowMode::Windowed
            ? draft.width
            : (draft.fullscreenWidth > 0 ? draft.fullscreenWidth : draft.width);
    const int height =
        draft.mode == Kotonoha::WindowMode::Windowed
            ? draft.height
            : (draft.fullscreenHeight > 0 ? draft.fullscreenHeight
                                          : draft.height);
    const float shownRefresh =
        draft.mode == Kotonoha::WindowMode::Windowed && native
            ? native->refresh_rate
            : draft.refreshRate;
    draw(Kotonoha::DisplayModeCatalog::Label({width, height, shownRefresh}) +
             (draft.mode == Kotonoha::WindowMode::Windowed ? " (window)" : ""),
         254.0f, 113.0f, 488.0f);
  }
  draw(Kotonoha::UpscaleBackendName(draft.upscaling.backend), 254.0f, 215.0f,
       488.0f);
  draw(draft.upscaling.shaderFiles.empty()
           ? Kotonoha::UpscalePresetName(draft.upscaling.preset)
           : "Custom GLSL",
       254.0f, 248.0f, 488.0f);
  draw(draft.upscaling.shaderFiles.empty()
           ? "(none)"
           : DisplayClipText(draft.upscaling.shaderFiles, 48),
       254.0f, 281.0f, 371.0f);

  if (desktopDisplayPopup == DesktopDisplayPopup::None)
    return;
  const auto outer =
      canvas.map(kDesktopPopupX, kDesktopPopupY, kDesktopPopupWidth, 330.0f);
  ui.panel(renderer, outer, {17, 27, 43, 248});
  const SDL_Color light{247, 248, 253, 255};
  const auto popupText = [&](const std::string &value, float x, float y,
                             float width) {
    const auto rect = canvas.map(x, y, width, 23.0f);
    ui.text(renderer, value, rect.x, rect.y, rect.w, light);
  };
  if (desktopDisplayPopup == DesktopDisplayPopup::GlslPath) {
    popupText("CUSTOM GLSL FILE - Enter to accept / Esc to cancel",
              kDesktopPopupX + 13, kDesktopPopupY + 13,
              kDesktopPopupWidth - 26);
    const auto field = canvas.map(kDesktopPopupX + 12, kDesktopPopupY + 61,
                                  kDesktopPopupWidth - 24, 52);
    ui.panel(renderer, field, {231, 235, 243, 255});
    const auto line = canvas.map(kDesktopPopupX + 19, kDesktopPopupY + 73,
                                 kDesktopPopupWidth - 38, 28);
    ui.text(renderer, DisplayClipText(desktopDisplayPathEdit + "|", 58), line.x,
            line.y, line.w, ink);
    popupText("Paste full path to an existing .glsl / .hook / .frag file",
              kDesktopPopupX + 13, kDesktopPopupY + 131,
              kDesktopPopupWidth - 26);
    popupText("Empty input clears the custom shader override.",
              kDesktopPopupX + 13, kDesktopPopupY + 159,
              kDesktopPopupWidth - 26);
    const auto ok =
        canvas.map(kDesktopPopupX + 40, kDesktopPopupY + 248, 185, 45);
    const auto cancel =
        canvas.map(kDesktopPopupX + 283, kDesktopPopupY + 248, 185, 45);
    ui.button(renderer, ok, "OK (ENTER)", desktopDisplayPopupHover == 0);
    ui.button(renderer, cancel, "CANCEL (ESC)", desktopDisplayPopupHover == 1);
    return;
  }

  std::vector<std::string> names;
  std::string title;
  if (desktopDisplayPopup == DesktopDisplayPopup::Resolution) {
    title = "RESOLUTION / REFRESH - WHEEL / UP / DOWN";
    names.reserve(desktopDisplayModes.size());
    for (const auto &mode : desktopDisplayModes)
      names.push_back(Kotonoha::DisplayModeCatalog::Label(mode));
  } else if (desktopDisplayPopup == DesktopDisplayPopup::Upscaler) {
    title = "UPSCALER BACKEND";
    const std::array<Kotonoha::UpscaleBackend, 5> backends{
        Kotonoha::UpscaleBackend::None, Kotonoha::UpscaleBackend::Linear,
        Kotonoha::UpscaleBackend::MPV, Kotonoha::UpscaleBackend::MetalFX,
        Kotonoha::UpscaleBackend::Auto};
    for (const auto backend : backends) {
      names.push_back(
          std::string(Kotonoha::UpscaleBackendName(backend)) +
          (engine->GetUpscaler().supports(backend) ? "" : " (unavailable)"));
    }
  } else {
    title = "SHADER PRESET - 15 MPV GLSL PROFILES";
    for (const auto preset : Kotonoha::UpscalePresets())
      names.push_back(Kotonoha::UpscalePresetName(preset));
  }
  const int maxScroll =
      std::max(0, static_cast<int>(names.size()) - kDesktopPopupRows);
  desktopDisplayPopupScroll =
      std::clamp(desktopDisplayPopupScroll, 0, maxScroll);
  popupText(title, kDesktopPopupX + 12, kDesktopPopupY + 11,
            kDesktopPopupWidth - 24);
  for (int row = 0; row < kDesktopPopupRows; ++row) {
    const int index = desktopDisplayPopupScroll + row;
    if (index >= static_cast<int>(names.size()))
      break;
    const auto rect = canvas.map(kDesktopPopupX + 12,
                                 kDesktopPopupY + 39 + row * kDesktopPopupRow,
                                 kDesktopPopupWidth - 24, kDesktopPopupRow - 2);
    ui.button(renderer, rect, names[static_cast<size_t>(index)],
              desktopDisplayPopupHover == index);
  }
  if (maxScroll > 0) {
    char page[64];
    SDL_snprintf(page, sizeof(page), "%d-%d / %d",
                 desktopDisplayPopupScroll + 1,
                 std::min(static_cast<int>(names.size()),
                          desktopDisplayPopupScroll + kDesktopPopupRows),
                 static_cast<int>(names.size()));
    popupText(page, kDesktopPopupX + 355, kDesktopPopupY + 309, 140);
  }
}

bool Runtime::handleDesktopDisplayPanel(Kotonoha::EventRef event) {
  if (!displayPanelOpen() || !desktopDisplayDraftActive ||
      desktopDisplayPopup == DesktopDisplayPopup::None)
    return false;
  auto popup = desktopDisplayPopup;
  const bool editingPath = popup == DesktopDisplayPopup::GlslPath;
  const auto finishPopup = [&] {
    if (editingPath)
      SDL_StopTextInput(&engine->GetWindow());
    desktopDisplayPopup = DesktopDisplayPopup::None;
    desktopDisplayPopupHover = -1;
    desktopDisplayPopupScroll = 0;
  };
  const int count = popup == DesktopDisplayPopup::Resolution
                        ? static_cast<int>(desktopDisplayModes.size())
                    : popup == DesktopDisplayPopup::Upscaler ? 5
                    : popup == DesktopDisplayPopup::Shader   ? 15
                                                             : 2;
  const int maxScroll = std::max(0, count - kDesktopPopupRows);
  const auto choose = [&](int choice) {
    if (editingPath) {
      if (choice == 1) {
        finishPopup();
        playSound(Ui::UiSfxCue::Cancel);
        return;
      }
      if (!desktopDisplayPathEdit.empty()) {
        try {
          const std::filesystem::path path =
              std::filesystem::u8path(desktopDisplayPathEdit);
          std::error_code ec;
          std::string extension = path.extension().string();
          std::transform(extension.begin(), extension.end(), extension.begin(),
                         [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                         });
          const bool suffix = extension == ".glsl" || extension == ".hook" ||
                              extension == ".frag";
          if (!suffix || !std::filesystem::is_regular_file(path, ec)) {
            setStatus("GLSL path must point to an existing .glsl, .hook or "
                      ".frag file");
            return;
          }
        } catch (const std::exception &) {
          setStatus("GLSL path contains invalid characters");
          return;
        }
      }
      desktopDisplayDraft.upscaling.shaderFiles = desktopDisplayPathEdit;
      finishPopup();
      playSound(Ui::UiSfxCue::Click);
      return;
    }
    if (choice < 0 || choice >= count)
      return;
    if (popup == DesktopDisplayPopup::Resolution) {
      const auto &chosen = desktopDisplayModes[static_cast<size_t>(choice)];
      const SDL_DisplayID display =
          DisplayIdForIndex(desktopDisplayDraft.display);
      const auto *native =
          display ? SDL_GetDesktopDisplayMode(display) : nullptr;
      if (desktopDisplayDraft.mode == Kotonoha::WindowMode::Borderless &&
          (!native || native->w != chosen.width ||
           native->h != chosen.height)) {
        setStatus("Borderless uses the current desktop resolution; select "
                  "WINDOW in General to change size");
        finishPopup();
        return;
      }
      // Preserve mode. The generic catalog normally promotes non-native
      // Borderless to Exclusive, which this page is expressly forbidden to do.
      const auto effective =
          desktopDisplayDraft.mode == Kotonoha::WindowMode::Borderless
              ? Kotonoha::DisplayModeChoice{native->w, native->h,
                                            native->refresh_rate}
              : chosen;
      Kotonoha::DisplayModeCatalog::Select(desktopDisplayDraft, effective,
                                           native);
      if (desktopDisplayDraft.mode == Kotonoha::WindowMode::Windowed &&
          native) {
        // A decorated window may resize its client but cannot change the
        // monitor refresh rate. Never persist a fictitious 144/240 Hz switch.
        desktopDisplayDraft.refreshRate = native->refresh_rate;
        setStatus(
            "Windowed size selected; monitor refresh remains at desktop rate");
      }
    } else if (popup == DesktopDisplayPopup::Upscaler) {
      constexpr std::array<Kotonoha::UpscaleBackend, 5> backends{
          Kotonoha::UpscaleBackend::None, Kotonoha::UpscaleBackend::Linear,
          Kotonoha::UpscaleBackend::MPV, Kotonoha::UpscaleBackend::MetalFX,
          Kotonoha::UpscaleBackend::Auto};
      const auto backend = backends[static_cast<size_t>(choice)];
      if (!engine->GetUpscaler().supports(backend)) {
        setStatus("This upscaler is unavailable on the active renderer");
        return;
      }
      desktopDisplayDraft.upscaling.backend = backend;
    } else if (popup == DesktopDisplayPopup::Shader) {
      desktopDisplayDraft.upscaling.preset =
          Kotonoha::UpscalePresets()[static_cast<size_t>(choice)];
      desktopDisplayDraft.upscaling.shaderFiles.clear();
    }
    finishPopup();
    playSound(Ui::UiSfxCue::Click);
    systemUi.SetConfigVisualState(configVisualState());
  };
  if (event.type == SDL_EVENT_TEXT_INPUT && editingPath) {
    if (event.text.text) {
      for (const char *p = event.text.text;
           *p && desktopDisplayPathEdit.size() < 1024; ++p)
        if (*p != '\n' && *p != '\r')
          desktopDisplayPathEdit.push_back(*p);
    }
    return true;
  }
  if (event.type == SDL_EVENT_KEY_DOWN) {
    if (event.key.repeat)
      return true;
    if (event.key.key == SDLK_ESCAPE) {
      finishPopup();
      playSound(Ui::UiSfxCue::Cancel);
      return true;
    }
    if (editingPath) {
      if (event.key.key == SDLK_BACKSPACE && !desktopDisplayPathEdit.empty()) {
        do {
          desktopDisplayPathEdit.pop_back();
        } while (!desktopDisplayPathEdit.empty() &&
                 (static_cast<unsigned char>(desktopDisplayPathEdit.back()) &
                  0xc0u) == 0x80u);
      } else if (event.key.key == SDLK_V &&
                 (SDL_GetModState() & SDL_KMOD_CTRL)) {
        if (char *clipboard = SDL_GetClipboardText()) {
          for (const char *p = clipboard;
               *p && desktopDisplayPathEdit.size() < 1024; ++p)
            if (*p != '\n' && *p != '\r')
              desktopDisplayPathEdit.push_back(*p);
          SDL_free(clipboard);
        }
      } else if (event.key.key == SDLK_RETURN ||
                 event.key.key == SDLK_KP_ENTER) {
        choose(0);
      }
      return true;
    }
    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
      choose(desktopDisplayPopupHover >= 0 ? desktopDisplayPopupHover
                                           : desktopDisplayPopupScroll);
      return true;
    }
    const int step = event.key.key == SDLK_UP         ? -1
                     : event.key.key == SDLK_DOWN     ? 1
                     : event.key.key == SDLK_PAGEUP   ? -kDesktopPopupRows
                     : event.key.key == SDLK_PAGEDOWN ? kDesktopPopupRows
                                                      : 0;
    if (step != 0) {
      desktopDisplayPopupHover =
          std::clamp(desktopDisplayPopupHover + step, 0, count - 1);
    } else if (event.key.key == SDLK_HOME)
      desktopDisplayPopupHover = 0;
    else if (event.key.key == SDLK_END)
      desktopDisplayPopupHover = count - 1;
    else
      return true;
    desktopDisplayPopupScroll =
        std::clamp(desktopDisplayPopupScroll,
                   desktopDisplayPopupHover - kDesktopPopupRows + 1,
                   std::max(desktopDisplayPopupHover, 0));
    desktopDisplayPopupScroll =
        std::clamp(desktopDisplayPopupScroll, 0, maxScroll);
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    if (!editingPath) {
      desktopDisplayPopupScroll =
          std::clamp(desktopDisplayPopupScroll -
                         static_cast<int>(std::round(event.wheel.y)),
                     0, maxScroll);
      // Keyboard Enter must select a row still visible after wheel scrolling,
      // not the previous offscreen highlight.
      desktopDisplayPopupHover = std::min(count - 1, desktopDisplayPopupScroll);
    }
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_MOTION ||
      (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
       event.button.button == SDL_BUTTON_LEFT)) {
    const float x =
        event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
    const float y =
        event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
    const auto canvas = DesktopCanvas(
        systemUi.ConfigMenu().CurrentVariant(), engine->screenSize(),
        narrativePresentationHeightScale() < 0.999f);
    const auto popupRect =
        canvas.map(kDesktopPopupX, kDesktopPopupY, kDesktopPopupWidth, 330.0f);
    int hovered = -1;
    if (PointIn(x, y, popupRect)) {
      if (editingPath) {
        if (PointIn(
                x, y,
                canvas.map(kDesktopPopupX + 40, kDesktopPopupY + 248, 185, 45)))
          hovered = 0;
        else if (PointIn(x, y,
                         canvas.map(kDesktopPopupX + 283, kDesktopPopupY + 248,
                                    185, 45)))
          hovered = 1;
      } else {
        for (int visible = 0; visible < kDesktopPopupRows; ++visible) {
          const int index = desktopDisplayPopupScroll + visible;
          if (index >= count)
            break;
          if (PointIn(
                  x, y,
                  canvas.map(kDesktopPopupX + 12,
                             kDesktopPopupY + 39 + visible * kDesktopPopupRow,
                             kDesktopPopupWidth - 24, kDesktopPopupRow - 2))) {
            hovered = index;
            break;
          }
        }
      }
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      desktopDisplayPopupHover = hovered;
    } else {
      if (hovered >= 0)
        choose(hovered);
      else {
        finishPopup(); // No click-through to underlying Config CMAP.
        playSound(Ui::UiSfxCue::Cancel);
      }
    }
    return true;
  }
  // Popup is modal: no underlying tab/close or system controls can activate.
  return true;
}
#else
void Runtime::discardDesktopDisplayDraft() {}
void Runtime::resetDesktopDisplayDraft() {}
void Runtime::activateDesktopDisplayControl(int) {}
void Runtime::renderDesktopDisplayPanel(Kotonoha::RendererRef) {}
bool Runtime::handleDesktopDisplayPanel(Kotonoha::EventRef) { return false; }
#endif

bool Runtime::displayPanelOpen() const {
  return engine && systemUi.IsConfigOpen() &&
         systemUi.ConfigMenu().CurrentPage() ==
             Kotonoha::SchoolDaysConfigMenuModel::Page::Display;
}

bool Runtime::applyDisplayAspectPreference() {
  if (!engine)
    return false;
  DisplayPolicy::Variant requested = uiVariant;
  if (displayAspectPreference == 1) {
    requested = DisplayPolicy::Variant::Standard;
  } else if (displayAspectPreference == 2) {
    requested = DisplayPolicy::Variant::Wide;
  } else {
    int width = 0, height = 0;
    DisplayPolicy::Decision automatic;
    if (!SDL_GetRenderOutputSize(&engine->GetRenderer(), &width, &height) ||
        !DisplayPolicy::SelectAuto(width, height, &automatic))
      return false;
    requested = automatic.variant;
  }
  const auto canonical =
      DisplayPolicy::CanonicalRetailStateForVariant(requested);
  displayBackend.baseDisplayType = canonical.effectiveDisplayType;
  displayBackend.deviceDisplayType = canonical.effectiveDisplayType;
  displayBackend.typeMiniNote = canonical.typeMiniNote;
  // Aspect changes only select the authored layout. The physical host mode
  // was loaded separately from settings.json and must survive this projection.
  refreshDisplayProjection();
  return true;
}

void Runtime::renderDisplayPanel(Kotonoha::RendererRef renderer) {
  if (!engine || !systemUi.IsConfigOpen())
    return;
#if !defined(__ANDROID__) && !defined(ANDROID)
  renderDesktopDisplayPanel(renderer);
  return;
#endif
  const SDL_FPoint screen = engine->screenSize();
  const auto geometry = DisplayGeometry(systemUi.ConfigMenu().CurrentVariant(),
                                        screen.x, screen.y);
  if (geometry.tab.w <= 0.0f || geometry.tab.h <= 0.0f)
    return;

  auto &ui = engine->GetUI();
  const bool active = displayPanelOpen();
  ui.panel(renderer, geometry.tab,
           active ? SDL_Color{186, 205, 244, 255}
                  : SDL_Color{235, 238, 242, 252});
  ui.text(renderer, "DISPLAY OPTIONS", geometry.tab.x + 12.0f,
          geometry.tab.y + 8.0f, geometry.tab.w - 24.0f, {35, 42, 58, 255});
  if (!active || geometry.panel.w <= 0.0f || geometry.panel.h <= 0.0f)
    return;

  ui.panel(renderer, geometry.panel, {12, 15, 24, 242});
#if defined(__ANDROID__) || defined(ANDROID)
  ui.text(renderer, "DISPLAY / ANDROID", geometry.panel.x + 28.0f,
          geometry.panel.y + 15.0f, geometry.panel.w - 56.0f,
          {240, 240, 245, 255});
  const auto &settings = engine->GetSettings();
  const auto modes = AndroidDisplayModesUnique();
  const int selectedMode = SelectedAndroidMode(modes, settings);
  displayPanelSelectedResolution = selectedMode;

  constexpr const char *aspectLabels[]{"Auto (device)", "4:3", "16:9"};
  const float contentAspect = uiVariant == DisplayPolicy::Variant::Standard
                                  ? 4.0f / 3.0f
                                  : 16.0f / 9.0f;
  const float outputAspect = screen.y > 0 ? screen.x / screen.y : contentAspect;
  const int baseWidth =
      outputAspect < contentAspect
          ? std::max(1, static_cast<int>(std::lround(screen.x)))
          : std::max(1,
                     static_cast<int>(std::lround(screen.y * contentAspect)));
  const int baseHeight =
      outputAspect < contentAspect
          ? std::max(1, static_cast<int>(std::lround(screen.x / contentAspect)))
          : std::max(1, static_cast<int>(std::lround(screen.y)));
  const int renderWidth = std::max(
      1, static_cast<int>(std::lround(baseWidth * settings.renderScale)));
  const int renderHeight = std::max(
      1, static_cast<int>(std::lround(baseHeight * settings.renderScale)));
  char aspect[96], renderResolution[128], frameRate[96], upscaling[160];
  SDL_snprintf(aspect, sizeof(aspect), "Aspect: %s",
               aspectLabels[std::clamp(displayAspectPreference, 0, 2)]);
  SDL_snprintf(renderResolution, sizeof(renderResolution),
               "Render Resolution: %dx%d (%d%%)", renderWidth, renderHeight,
               static_cast<int>(std::lround(settings.renderScale * 100.0f)));
  if (selectedMode >= 0 && selectedMode < static_cast<int>(modes.size())) {
    char formatted[64];
    FormatAndroidFrameRate(modes[static_cast<size_t>(selectedMode)], formatted,
                           sizeof(formatted));
    SDL_snprintf(frameRate, sizeof(frameRate), "Frame Rate: %s", formatted);
  } else {
    SDL_snprintf(frameRate, sizeof(frameRate), "Frame Rate: Device default");
  }
  const int preset = CurrentDisplayPreset(settings);
  SDL_snprintf(upscaling, sizeof(upscaling), "Upscaling: %s",
               DisplayPresetLabels[preset]);
  const std::array<const char *, 4> labels{aspect, renderResolution, frameRate,
                                           upscaling};
  for (int row = 1; row <= 4; ++row)
    ui.button(renderer, DisplayRow(geometry, row),
              labels[static_cast<size_t>(row - 1)],
              displayPanelHoverRow == row);

  if (displayPanelRenderScaleOpen) {
    const auto popup = AndroidRenderScalePopup(geometry);
    ui.panel(renderer, popup.rect, {8, 11, 18, 252});
    ui.text(renderer, "RENDER RESOLUTION", popup.rect.x + 12.0f,
            popup.rect.y + 7.0f, popup.rect.w - 24.0f, {240, 240, 245, 255});
    for (int index = 0; index < static_cast<int>(kAndroidRenderScales.size());
         ++index) {
      const SDL_FRect item{popup.rect.x + 8.0f,
                           popup.rect.y + popup.headerHeight +
                               index * popup.rowHeight,
                           popup.rect.w - 16.0f, popup.rowHeight - 3.0f};
      char text[160];
      const int width =
          std::max(1, static_cast<int>(std::lround(
                          baseWidth * kAndroidRenderScales[index])));
      const int height =
          std::max(1, static_cast<int>(std::lround(
                          baseHeight * kAndroidRenderScales[index])));
      const bool chosen =
          std::abs(settings.renderScale - kAndroidRenderScales[index]) < 0.01f;
      SDL_snprintf(
          text, sizeof(text), "%s %s  %dx%d (%d%%)", chosen ? ">" : " ",
          kAndroidRenderScaleLabels[index], width, height,
          static_cast<int>(std::lround(kAndroidRenderScales[index] * 100.0f)));
      ui.button(renderer, item, text, displayPanelHoverRenderScale == index);
    }
  }
  if (displayPanelResolutionOpen && !modes.empty()) {
    const auto popup = DisplayResolutionPopup(geometry);
    const int maxScroll =
        std::max(0, static_cast<int>(modes.size()) - popup.visibleRows);
    displayPanelResolutionScroll =
        std::clamp(displayPanelResolutionScroll, 0, maxScroll);
    ui.panel(renderer, popup.rect, {8, 11, 18, 252});
    ui.text(renderer, "SUPPORTED FRAME RATES", popup.rect.x + 12.0f,
            popup.rect.y + 7.0f, popup.rect.w - 24.0f, {240, 240, 245, 255});
    for (int visible = 0; visible < popup.visibleRows; ++visible) {
      const int index = displayPanelResolutionScroll + visible;
      if (index >= static_cast<int>(modes.size()))
        break;
      const SDL_FRect item{popup.rect.x + 8.0f,
                           popup.rect.y + popup.headerHeight +
                               visible * popup.rowHeight,
                           popup.rect.w - 16.0f, popup.rowHeight - 3.0f};
      char formatted[64], text[96];
      FormatAndroidFrameRate(modes[static_cast<size_t>(index)], formatted,
                             sizeof(formatted));
      SDL_snprintf(text, sizeof(text), "%s %s",
                   index == selectedMode ? ">" : " ", formatted);
      ui.button(renderer, item, text, displayPanelHoverResolution == index);
    }
  }
  if (displayPanelPresetOpen) {
    const auto popup = DisplayPresetPopup(geometry);
    ui.panel(renderer, popup.rect, {8, 11, 18, 252});
    ui.text(renderer, "ANIME UPSCALER PRESETS", popup.rect.x + 12.0f,
            popup.rect.y + 7.0f, popup.rect.w - 24.0f, {240, 240, 245, 255});
    for (int index = 0; index < 5; ++index) {
      const SDL_FRect item{popup.rect.x + 8.0f,
                           popup.rect.y + popup.headerHeight +
                               index * popup.rowHeight,
                           popup.rect.w - 16.0f, popup.rowHeight - 3.0f};
      const std::string label = std::string(index == preset ? "> " : "  ") +
                                DisplayPresetLabels[index];
      ui.button(renderer, item, label, displayPanelHoverPreset == index);
    }
  }
  return;
#else
  ui.text(renderer, "DISPLAY / REMASTER", geometry.panel.x + 28.0f,
          geometry.panel.y + 15.0f, geometry.panel.w - 56.0f,
          {240, 240, 245, 255});

  const auto &settings = engine->GetSettings();
  const auto resolutions = DisplayResolutions(settings.display);
  // Refresh selection from the current physical settings, including changes
  // made via General Window/Full or a previously chosen Windowed resolution.
  displayPanelSelectedResolution =
      SelectedDisplayResolution(resolutions, settings);

  constexpr const char *aspectLabels[]{"Auto", "4:3", "16:9"};
  char aspect[96], mode[96], resolution[128], monitor[96], upscaling[160],
      vsync[64];
  SDL_snprintf(aspect, sizeof(aspect), "Aspect: %s",
               aspectLabels[std::clamp(displayAspectPreference, 0, 2)]);
  SDL_snprintf(mode, sizeof(mode), "Mode: %s", WindowModeLabel(settings.mode));
  SDL_snprintf(monitor, sizeof(monitor), "Monitor: Display %d",
               settings.display + 1);
  if (displayPanelSelectedResolution >= 0 &&
      displayPanelSelectedResolution < static_cast<int>(resolutions.size())) {
    char selected[96];
    FormatDisplayResolution(
        resolutions[static_cast<size_t>(displayPanelSelectedResolution)],
        selected, sizeof(selected));
    SDL_snprintf(resolution, sizeof(resolution), "Resolution: %s", selected);
  } else {
    SDL_snprintf(resolution, sizeof(resolution), "Resolution: Desktop/native");
  }
  const int preset = CurrentDisplayPreset(settings);
  SDL_snprintf(upscaling, sizeof(upscaling), "Upscaling: %s",
               DisplayPresetLabels[preset]);
  SDL_snprintf(vsync, sizeof(vsync), "V-Sync: %s",
               settings.vsync ? "ON" : "OFF");
  const std::array<const char *, 6> labels{aspect,     mode,      monitor,
                                           resolution, upscaling, vsync};
  for (int row = 1; row <= 6; ++row)
    ui.button(renderer, DisplayRow(geometry, row),
              labels[static_cast<size_t>(row - 1)],
              displayPanelHoverRow == row);

  if (displayPanelResolutionOpen && !resolutions.empty()) {
    const auto popup = DisplayResolutionPopup(geometry);
    const int maxScroll =
        std::max(0, static_cast<int>(resolutions.size()) - popup.visibleRows);
    displayPanelResolutionScroll =
        std::clamp(displayPanelResolutionScroll, 0, maxScroll);
    ui.panel(renderer, popup.rect, {8, 11, 18, 252});
    char header[128];
    const int first = displayPanelResolutionScroll + 1;
    const int last = std::min(static_cast<int>(resolutions.size()),
                              displayPanelResolutionScroll + popup.visibleRows);
    SDL_snprintf(header, sizeof(header),
                 "RESOLUTIONS %d-%d / %d - WHEEL/PGUP/PGDN", first, last,
                 static_cast<int>(resolutions.size()));
    ui.text(renderer, header, popup.rect.x + 12.0f, popup.rect.y + 7.0f,
            popup.rect.w - 24.0f, {240, 240, 245, 255});
    for (int visible = 0; visible < popup.visibleRows; ++visible) {
      const int index = displayPanelResolutionScroll + visible;
      if (index >= static_cast<int>(resolutions.size()))
        break;
      const SDL_FRect item{popup.rect.x + 8.0f,
                           popup.rect.y + popup.headerHeight +
                               visible * popup.rowHeight,
                           popup.rect.w - 16.0f, popup.rowHeight - 3.0f};
      char itemText[128];
      char modeText[96];
      FormatDisplayResolution(resolutions[static_cast<size_t>(index)], modeText,
                              sizeof(modeText));
      SDL_snprintf(itemText, sizeof(itemText), "%s %s",
                   index == displayPanelSelectedResolution ? ">" : " ",
                   modeText);
      ui.button(renderer, item, itemText, displayPanelHoverResolution == index);
    }
    // Always expose the catalog's actual length and current position. Before
    // this, opening the popup in its middle looked like a short, incomplete
    // list even though many modes were reachable by the wheel.
    if (maxScroll > 0) {
      const float trackHeight = popup.rect.h - popup.headerHeight - 14.0f;
      const float knobHeight =
          std::max(12.0f, trackHeight * static_cast<float>(popup.visibleRows) /
                              static_cast<float>(resolutions.size()));
      const float knobY =
          popup.rect.y + popup.headerHeight + 7.0f +
          (trackHeight - knobHeight) *
              (static_cast<float>(displayPanelResolutionScroll) /
               static_cast<float>(maxScroll));
      ui.panel(renderer,
               {popup.rect.x + popup.rect.w - 7.0f,
                popup.rect.y + popup.headerHeight + 7.0f, 3.0f, trackHeight},
               {90, 100, 115, 200});
      ui.panel(renderer,
               {popup.rect.x + popup.rect.w - 9.0f, knobY, 7.0f, knobHeight},
               {180, 201, 240, 255});
    }
  }

  if (!displayPanelPresetOpen)
    return;
  const auto popup = DisplayPresetPopup(geometry);
  ui.panel(renderer, popup.rect, {8, 11, 18, 252});
  ui.text(renderer, "ANIME UPSCALER PRESETS", popup.rect.x + 12.0f,
          popup.rect.y + 7.0f, popup.rect.w - 24.0f, {240, 240, 245, 255});
  for (int index = 0; index < 5; ++index) {
    const SDL_FRect item{popup.rect.x + 8.0f,
                         popup.rect.y + popup.headerHeight +
                             index * popup.rowHeight,
                         popup.rect.w - 16.0f, popup.rowHeight - 3.0f};
    const std::string label =
        std::string(index == preset ? "> " : "  ") + DisplayPresetLabels[index];
    ui.button(renderer, item, label, displayPanelHoverPreset == index);
  }
#endif
}

bool Runtime::handleDisplayPanel(Kotonoha::EventRef event) {
  if (!displayPanelOpen())
    return false;
#if !defined(__ANDROID__) && !defined(ANDROID)
  return handleDesktopDisplayPanel(event);
#endif
#if defined(__ANDROID__) || defined(ANDROID)
  const auto modes = AndroidDisplayModesUnique();
  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    if (!displayPanelResolutionOpen)
      return false;
    const SDL_FPoint screen = engine->screenSize();
    const auto popup = DisplayResolutionPopup(DisplayGeometry(
        systemUi.ConfigMenu().CurrentVariant(), screen.x, screen.y));
    const int maxScroll =
        std::max(0, static_cast<int>(modes.size()) - popup.visibleRows);
    if (event.wheel.y > 0.0f)
      --displayPanelResolutionScroll;
    else if (event.wheel.y < 0.0f)
      ++displayPanelResolutionScroll;
    displayPanelResolutionScroll =
        std::clamp(displayPanelResolutionScroll, 0, maxScroll);
    return true;
  }

  const bool mouseMotion = event.type == SDL_EVENT_MOUSE_MOTION;
  const bool mouseDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
  const bool fingerMotion = event.type == SDL_EVENT_FINGER_MOTION;
  const bool fingerDown = event.type == SDL_EVENT_FINGER_DOWN;
  if (!mouseMotion && !mouseDown && !fingerMotion && !fingerDown)
    return false;

  const SDL_FPoint screen = engine->screenSize();
  float x = 0.0f, y = 0.0f;
  if (mouseMotion) {
    x = event.motion.x;
    y = event.motion.y;
  } else if (mouseDown) {
    if (event.button.button != SDL_BUTTON_LEFT)
      return false;
    x = event.button.x;
    y = event.button.y;
  } else {
    // Engine::Event already converted finger input into Screen coordinates.
    x = event.tfinger.x;
    y = event.tfinger.y;
  }
  const bool activated = mouseDown || fingerDown;
  const auto geometry = DisplayGeometry(systemUi.ConfigMenu().CurrentVariant(),
                                        screen.x, screen.y);
  int hoveredRow = -1;
  for (int row = 1; row <= 4; ++row)
    if (PointIn(x, y, DisplayRow(geometry, row))) {
      hoveredRow = row;
      break;
    }

  const auto scalePopup = AndroidRenderScalePopup(geometry);
  int hoveredScale = -1;
  if (displayPanelRenderScaleOpen && PointIn(x, y, scalePopup.rect)) {
    const float relative = y - scalePopup.rect.y - scalePopup.headerHeight;
    if (relative >= 0.0f) {
      const int index = static_cast<int>(relative / scalePopup.rowHeight);
      if (index >= 0 && index < static_cast<int>(kAndroidRenderScales.size()))
        hoveredScale = index;
    }
  }
  const auto modePopup = DisplayResolutionPopup(geometry);
  int hoveredMode = -1;
  if (displayPanelResolutionOpen && PointIn(x, y, modePopup.rect)) {
    const float relative = y - modePopup.rect.y - modePopup.headerHeight;
    if (relative >= 0.0f) {
      const int visible = static_cast<int>(relative / modePopup.rowHeight);
      const int index = displayPanelResolutionScroll + visible;
      if (visible >= 0 && visible < modePopup.visibleRows && index >= 0 &&
          index < static_cast<int>(modes.size()))
        hoveredMode = index;
    }
  }
  const auto presetPopup = DisplayPresetPopup(geometry);
  int hoveredPreset = -1;
  if (displayPanelPresetOpen && PointIn(x, y, presetPopup.rect)) {
    const float relative = y - presetPopup.rect.y - presetPopup.headerHeight;
    if (relative >= 0.0f) {
      const int index = static_cast<int>(relative / presetPopup.rowHeight);
      if (index >= 0 && index < 5)
        hoveredPreset = index;
    }
  }

  displayPanelHoverRow = hoveredRow;
  displayPanelHoverRenderScale = hoveredScale;
  displayPanelHoverResolution = hoveredMode;
  displayPanelHoverPreset = hoveredPreset;
  if (!activated)
    return PointIn(x, y, geometry.panel) || PointIn(x, y, geometry.tab) ||
           (displayPanelRenderScaleOpen && PointIn(x, y, scalePopup.rect)) ||
           (displayPanelResolutionOpen && PointIn(x, y, modePopup.rect)) ||
           (displayPanelPresetOpen && PointIn(x, y, presetPopup.rect));

  auto settings = engine->GetSettings();
  if (displayPanelRenderScaleOpen && hoveredScale >= 0) {
    settings.renderScale =
        kAndroidRenderScales[static_cast<size_t>(hoveredScale)];
    engine->applySettings(settings);
    displayPanelRenderScaleOpen = false;
    playSound(Ui::UiSfxCue::Click);
    return true;
  }
  if (displayPanelResolutionOpen && hoveredMode >= 0) {
    const auto &mode = modes[static_cast<size_t>(hoveredMode)];
    // Engine::applyWindowSettings performs the JNI request and persistence as
    // one settings transaction; requesting it here as well can apply twice.
    settings.mode = Kotonoha::WindowMode::Borderless;
    settings.display = 0;
    settings.fullscreenWidth = mode.width;
    settings.fullscreenHeight = mode.height;
    settings.refreshRate = mode.refreshRate;
    settings.vsync = true;
    engine->applySettings(settings);
    displayPanelSelectedResolution = hoveredMode;
    displayPanelResolutionOpen = false;
    playSound(Ui::UiSfxCue::Click);
    return true;
  }
  if (displayPanelPresetOpen && hoveredPreset >= 0) {
    if (hoveredPreset == 0) {
      settings.upscaling.backend = Kotonoha::UpscaleBackend::None;
      settings.upscaling.factor = 1.0f;
    } else {
      settings.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
      settings.upscaling.preset =
          hoveredPreset == 1   ? Kotonoha::UpscalePreset::ArtCNN
          : hoveredPreset == 2 ? Kotonoha::UpscalePreset::ArtCNNDenoise
          : hoveredPreset == 3 ? Kotonoha::UpscalePreset::ArtCNNLarge
                               : Kotonoha::UpscalePreset::Anime4K;
      if (settings.upscaling.factor <= 1.0f)
        settings.upscaling.factor = 2.0f;
    }
    engine->applySettings(settings);
    displayPanelPresetOpen = false;
    playSound(Ui::UiSfxCue::Click);
    return true;
  }
  if (hoveredRow < 1)
    return PointIn(x, y, geometry.panel);

  if (hoveredRow == 1) {
    displayPanelRenderScaleOpen = false;
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = false;
    displayAspectPreference =
        (std::clamp(displayAspectPreference, 0, 2) + 1) % 3;
    const auto beforeVariant = uiVariant;
    if (applyDisplayAspectPreference()) {
      SaveRemasterAspectPreference(documents, displayAspectPreference);
      if (uiVariant != beforeVariant)
        openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Display, false);
    }
  } else if (hoveredRow == 2) {
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = false;
    displayPanelRenderScaleOpen = !displayPanelRenderScaleOpen;
  } else if (hoveredRow == 3) {
    displayPanelRenderScaleOpen = false;
    displayPanelPresetOpen = false;
    displayPanelResolutionOpen = !displayPanelResolutionOpen;
    if (displayPanelResolutionOpen && !modes.empty()) {
      const int selected = SelectedAndroidMode(modes, settings);
      const int maxScroll =
          std::max(0, static_cast<int>(modes.size()) - modePopup.visibleRows);
      displayPanelResolutionScroll =
          std::clamp(selected - modePopup.visibleRows / 2, 0, maxScroll);
    }
  } else if (hoveredRow == 4) {
    displayPanelRenderScaleOpen = false;
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = !displayPanelPresetOpen;
  }
  playSound(Ui::UiSfxCue::Click);
  return true;
#else
  if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    if (!displayPanelResolutionOpen)
      return false;
    const auto resolutions = DisplayResolutions(engine->GetSettings().display);
    const SDL_FPoint screen = engine->screenSize();
    const auto popup = DisplayResolutionPopup(DisplayGeometry(
        systemUi.ConfigMenu().CurrentVariant(), screen.x, screen.y));
    const int maxScroll =
        std::max(0, static_cast<int>(resolutions.size()) - popup.visibleRows);
    if (event.wheel.y > 0.0f)
      --displayPanelResolutionScroll;
    else if (event.wheel.y < 0.0f)
      ++displayPanelResolutionScroll;
    displayPanelResolutionScroll =
        std::clamp(displayPanelResolutionScroll, 0, maxScroll);
    displayPanelHoverResolution = -1;
    return true;
  }
  if (event.type != SDL_EVENT_MOUSE_MOTION &&
      event.type != SDL_EVENT_MOUSE_BUTTON_DOWN)
    return false;

  const float x =
      event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
  const float y =
      event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
  const SDL_FPoint screen = engine->screenSize();
  const auto geometry = DisplayGeometry(systemUi.ConfigMenu().CurrentVariant(),
                                        screen.x, screen.y);
  int hoveredRow = -1;
  for (int row = 1; row <= 6; ++row)
    if (PointIn(x, y, DisplayRow(geometry, row))) {
      hoveredRow = row;
      break;
    }

  const auto resolutions = DisplayResolutions(engine->GetSettings().display);
  const auto resolutionPopup = DisplayResolutionPopup(geometry);
  int hoveredResolution = -1;
  if (displayPanelResolutionOpen && PointIn(x, y, resolutionPopup.rect)) {
    const float relative =
        y - resolutionPopup.rect.y - resolutionPopup.headerHeight;
    if (relative >= 0.0f) {
      const int visible =
          static_cast<int>(relative / resolutionPopup.rowHeight);
      const int index = displayPanelResolutionScroll + visible;
      if (visible >= 0 && visible < resolutionPopup.visibleRows && index >= 0 &&
          index < static_cast<int>(resolutions.size()))
        hoveredResolution = index;
    }
  }

  const auto presetPopup = DisplayPresetPopup(geometry);
  int hoveredPreset = -1;
  if (displayPanelPresetOpen && PointIn(x, y, presetPopup.rect)) {
    const float relative = y - presetPopup.rect.y - presetPopup.headerHeight;
    if (relative >= 0.0f) {
      const int index = static_cast<int>(relative / presetPopup.rowHeight);
      if (index >= 0 && index < 5)
        hoveredPreset = index;
    }
  }

  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    displayPanelHoverRow = hoveredRow;
    displayPanelHoverResolution = hoveredResolution;
    displayPanelHoverPreset = hoveredPreset;
    return PointIn(x, y, geometry.panel) || PointIn(x, y, geometry.tab) ||
           (displayPanelResolutionOpen &&
            PointIn(x, y, resolutionPopup.rect)) ||
           (displayPanelPresetOpen && PointIn(x, y, presetPopup.rect));
  }
  if (event.button.button != SDL_BUTTON_LEFT)
    return false;

  auto settings = engine->GetSettings();
  if (displayPanelResolutionOpen && hoveredResolution >= 0) {
    const auto &selected = resolutions[static_cast<size_t>(hoveredResolution)];
    displayPanelSelectedResolution = hoveredResolution;
    const SDL_DisplayID display = DisplayIdForIndex(settings.display);
    const auto *desktop =
        display ? SDL_GetDesktopDisplayMode(display) : nullptr;
    if (!Kotonoha::SchoolDaysDesktopStartupProfile::SelectResolution(
            settings, selected.width, selected.height, selected.refreshRate,
            desktop ? desktop->w : 0, desktop ? desktop->h : 0,
            desktop ? desktop->refresh_rate : 0.0f))
      return true;
    engine->applySettings(settings);
    // A resolution change may promote Borderless to Exclusive. Keep General's
    // Window/Full selection synchronized with the actually applied host mode.
    displayBackend.windowMode =
        settings.mode == Kotonoha::WindowMode::Windowed ? 0 : 1;
    refreshDisplayProjection();
    std::string failure;
    Check(configStorage.CacheWindowMode(displayBackend.windowMode, &failure),
          failure, "School Days WindowMode cache update failed");
    displayPanelResolutionOpen = false;
    displayPanelHoverResolution = -1;
    playSound(Ui::UiSfxCue::Click);
    return true;
  }
  if (displayPanelPresetOpen && hoveredPreset >= 0) {
    if (hoveredPreset == 0) {
      settings.upscaling.backend = Kotonoha::UpscaleBackend::None;
      settings.upscaling.factor = 1.0f;
    } else {
      settings.upscaling.backend = Kotonoha::UpscaleBackend::Auto;
      settings.upscaling.preset =
          hoveredPreset == 1   ? Kotonoha::UpscalePreset::ArtCNN
          : hoveredPreset == 2 ? Kotonoha::UpscalePreset::ArtCNNDenoise
          : hoveredPreset == 3 ? Kotonoha::UpscalePreset::ArtCNNLarge
                               : Kotonoha::UpscalePreset::Anime4K;
      if (settings.upscaling.factor <= 1.0f)
        settings.upscaling.factor = 2.0f;
    }
    engine->applySettings(settings);
    displayPanelPresetOpen = false;
    displayPanelHoverPreset = -1;
    playSound(Ui::UiSfxCue::Click);
    return true;
  }
  if (displayPanelResolutionOpen && hoveredRow != 4) {
    displayPanelResolutionOpen = false;
    displayPanelHoverResolution = -1;
  }
  if (displayPanelPresetOpen && hoveredRow != 5) {
    displayPanelPresetOpen = false;
    displayPanelHoverPreset = -1;
  }
  if (hoveredRow < 1)
    return PointIn(x, y, geometry.panel);

  if (hoveredRow == 1) {
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = false;
    displayAspectPreference =
        (std::clamp(displayAspectPreference, 0, 2) + 1) % 3;
    const auto beforeVariant = uiVariant;
    if (!applyDisplayAspectPreference()) {
      setStatus("Display aspect selection failed");
      return true;
    }
    SaveRemasterAspectPreference(documents, displayAspectPreference);
    std::string failure;
    Check(configStorage.CacheDisplayType(displayBackend.baseDisplayType,
                                         &failure),
          failure, "School Days DisplayType cache update failed");
    Check(
        configStorage.CacheTypeMiniNote(displayBackend.typeMiniNote, &failure),
        failure, "School Days TypeMiniNote cache update failed");
    if (uiVariant != beforeVariant)
      openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Display, false);
  } else if (hoveredRow == 2) {
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = false;
    const auto beforeVariant = uiVariant;
    settings.mode = settings.mode == Kotonoha::WindowMode::Windowed
                        ? Kotonoha::WindowMode::Borderless
                    : settings.mode == Kotonoha::WindowMode::Borderless
                        ? Kotonoha::WindowMode::Exclusive
                        : Kotonoha::WindowMode::Windowed;
    if (settings.mode == Kotonoha::WindowMode::Exclusive &&
        displayPanelSelectedResolution >= 0 &&
        displayPanelSelectedResolution < static_cast<int>(resolutions.size())) {
      const auto &selected =
          resolutions[static_cast<size_t>(displayPanelSelectedResolution)];
      settings.fullscreenWidth = selected.width;
      settings.fullscreenHeight = selected.height;
      settings.refreshRate = selected.refreshRate;
    }
    engine->applySettings(settings);
    displayBackend.windowMode =
        settings.mode == Kotonoha::WindowMode::Windowed ? 0 : 1;
    refreshDisplayProjection();
    std::string failure;
    Check(configStorage.CacheWindowMode(displayBackend.windowMode, &failure),
          failure, "School Days WindowMode cache update failed");
    displayPanelSelectedResolution = -1;
    if (uiVariant != beforeVariant)
      openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Display, false);
  } else if (hoveredRow == 3) {
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = false;
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (count > 0) {
      settings.display = (settings.display + 1) % count;
      displayPanelSelectedResolution = -1;
      displayPanelResolutionScroll = 0;
      if (settings.mode == Kotonoha::WindowMode::Exclusive && displays) {
        if (const SDL_DisplayMode *desktop =
                SDL_GetDesktopDisplayMode(displays[settings.display])) {
          settings.fullscreenWidth = desktop->w;
          settings.fullscreenHeight = desktop->h;
          settings.refreshRate = desktop->refresh_rate;
        }
      }
      engine->applySettings(settings);
    }
    if (displays)
      SDL_free(displays);
  } else if (hoveredRow == 4) {
    displayPanelPresetOpen = false;
    displayPanelHoverPreset = -1;
    if (!resolutions.empty()) {
      displayPanelResolutionOpen = !displayPanelResolutionOpen;
      if (displayPanelResolutionOpen) {
        if (displayPanelSelectedResolution < 0 ||
            displayPanelSelectedResolution >=
                static_cast<int>(resolutions.size()))
          displayPanelSelectedResolution =
              SelectedDisplayResolution(resolutions, settings);
        const auto popup = DisplayResolutionPopup(geometry);
        const int maxScroll = std::max(0, static_cast<int>(resolutions.size()) -
                                              popup.visibleRows);
        displayPanelResolutionScroll =
            std::clamp(displayPanelSelectedResolution - popup.visibleRows / 2,
                       0, maxScroll);
      }
    }
  } else if (hoveredRow == 5) {
    displayPanelResolutionOpen = false;
    displayPanelHoverResolution = -1;
    displayPanelPresetOpen = !displayPanelPresetOpen;
    displayPanelHoverPreset = -1;
  } else if (hoveredRow == 6) {
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = false;
    settings.vsync = !settings.vsync;
    engine->applySettings(settings);
  }
  playSound(Ui::UiSfxCue::Click);
  return true;
#endif
}

void Runtime::renderStatus(Kotonoha::RendererRef renderer) {
  if (status.empty())
    return;
  if (SDL_GetTicks() - statusTime > StatusMilliseconds) {
    status.clear();
    return;
  }
  auto &ui = engine->GetUI();
  const float height = ui.textHeight(renderer, status, 1200) + 20;
  ui.panel(renderer, {30, 690 - height, 1220, height});
  ui.text(renderer, status, 40, 700 - height, 1200);
}

bool Runtime::onScreenEvent(Kotonoha::EventRef event) {
  if (currentState == Kotonoha::GameState::Splash) {
    const bool activation =
        (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_FINGER_DOWN;
    if (activation) {
      // Commercial host FinishSplash() dismissed the complete SPLASH.PNG ->
      // retail LOGO sequence with one activation, not one activation per owner.
      finishSplash();
      return true;
    }
  }
  if (!engine)
    return false;
#if !defined(__ANDROID__) && !defined(ANDROID)
  if (displayPanelOpen() && desktopDisplayPopup != DesktopDisplayPopup::None &&
      handleDesktopDisplayPanel(event))
    return true;
#endif
  if (transportFade.InputLocked())
    return true;
  if (exitTransition.InputLocked())
    return true;
  if (systemPanelTransition.InputLocked())
    return true;
  if (!systemUi.IsOpen()) {
    return currentState == Kotonoha::GameState::Gameplay && gameScene &&
           !panelOpen() && !frontendTransition.Active(SDL_GetTicks()) &&
           gameScene->handleMenuBarEvent(event);
  }
  if (displayPanelOpen() && displayPanelResolutionOpen &&
      event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
#if defined(_WIN32) && !defined(__ANDROID__)
    const auto modes = DisplayResolutions(engine->GetSettings().display);
    const SDL_FPoint screen = engine->screenSize();
    const auto popup = DisplayResolutionPopup(DisplayGeometry(
        systemUi.ConfigMenu().CurrentVariant(), screen.x, screen.y));
    const int maxScroll =
        std::max(0, static_cast<int>(modes.size()) - popup.visibleRows);
    const int page = std::max(1, popup.visibleRows - 1);
    switch (event.key.key) {
    case SDLK_UP:
      --displayPanelResolutionScroll;
      break;
    case SDLK_DOWN:
      ++displayPanelResolutionScroll;
      break;
    case SDLK_PAGEUP:
      displayPanelResolutionScroll -= page;
      break;
    case SDLK_PAGEDOWN:
      displayPanelResolutionScroll += page;
      break;
    case SDLK_HOME:
      displayPanelResolutionScroll = 0;
      break;
    case SDLK_END:
      displayPanelResolutionScroll = maxScroll;
      break;
    default:
      return false;
    }
    displayPanelResolutionScroll =
        std::clamp(displayPanelResolutionScroll, 0, maxScroll);
    displayPanelHoverResolution = -1;
    return true;
#endif
  }
  const bool pointer = event.type == SDL_EVENT_MOUSE_MOTION ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                       event.type == SDL_EVENT_MOUSE_WHEEL ||
                       event.type == SDL_EVENT_FINGER_DOWN ||
                       event.type == SDL_EVENT_FINGER_UP ||
                       event.type == SDL_EVENT_FINGER_MOTION;
  if (!pointer)
    return false;
  if (frontendTransition.Active(SDL_GetTicks()))
    return true;

  const SDL_FPoint screen = engine->screenSize();
  systemUi.SetArea({std::max(1, static_cast<int>(std::lround(screen.x))),
                    std::max(1, static_cast<int>(std::lround(screen.y)))});
  systemUi.SetConfigStandardFullscreenStretch(
      systemUi.IsConfigOpen() && narrativePresentationHeightScale() < 0.999f);
  try {
#if defined(__ANDROID__) || defined(ANDROID)
    // SDL emits a synthetic mouse click for the same finger activation. The
    // Display rows consume FINGER_DOWN directly; do not activate them twice.
    // Keep synthetic clicks outside this panel for the retail Config tabs.
    if (displayPanelOpen() && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.which == SDL_TOUCH_MOUSEID) {
      const auto geometry = DisplayGeometry(
          systemUi.ConfigMenu().CurrentVariant(), screen.x, screen.y);
      const SDL_FPoint point{event.button.x, event.button.y};
      const auto resolutionPopup = DisplayResolutionPopup(geometry);
      const auto scalePopup = AndroidRenderScalePopup(geometry);
      const auto presetPopup = DisplayPresetPopup(geometry);
      if (SDL_PointInRectFloat(&point, &geometry.panel) ||
          (displayPanelResolutionOpen &&
           SDL_PointInRectFloat(&point, &resolutionPopup.rect)) ||
          (displayPanelRenderScaleOpen &&
           SDL_PointInRectFloat(&point, &scalePopup.rect)) ||
          (displayPanelPresetOpen &&
           SDL_PointInRectFloat(&point, &presetPopup.rect)))
        return true;
    }
#endif
    if (handleDisplayPanel(event))
      return true;
    Ui::Output output;
    if (systemUi.HandleEvent(&engine->GetRenderer(), &event, &output))
      handlePanel(output);
  } catch (const std::exception &error) {
    setStatus(error.what());
  }
  // SystemUi is modal while open. Pointer events outside the fitted artwork
  // clear native hover state but must not leak through to the title/gameplay.
  return true;
}

bool Runtime::onEvent(Kotonoha::EventRef eventInput) {
  const SDL_Event event = Kotonoha::MapControllerEvent(
      eventInput, currentState == Kotonoha::GameState::Gameplay,
      backlogFallback && backlogFallback->isVisible());
  if (currentState == Kotonoha::GameState::Splash) {
    return Kotonoha::Runtime::onEvent(event);
  }
  if (exitTransition.InputLocked() && FrontendInputEvent(event.type))
    return true;
  if (transportFade.InputLocked() && FrontendInputEvent(event.type))
    return true;
  if (loadTransition.InputLocked() && FrontendInputEvent(event.type))
    return true;
  if (systemPanelTransition.InputLocked() && FrontendInputEvent(event.type))
    return true;
  if (frontendTransition.Active(SDL_GetTicks()) &&
      FrontendInputEvent(event.type))
    return true;
#if !defined(__ANDROID__) && !defined(ANDROID)
  // Screen events route pointer clicks separately. Key/text input for a modal
  // resolution/shader/GLSL editor must be consumed before the retail ESC close.
  if (displayPanelOpen() && desktopDisplayPopup != DesktopDisplayPopup::None &&
      handleDesktopDisplayPanel(event))
    return true;
#endif
  const bool escape = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                      event.key.key == SDLK_ESCAPE;
  if (saveMenu->isOpen()) {
    for (auto *scene : GetActiveScenes())
      if (auto *dialog = dynamic_cast<Kotonoha::TextInputDialog *>(scene)) {
        dispatchEvent(*dialog, event);
        return true;
      }
    dispatchEvent(*saveMenu, event);
    return true;
  }
  if (backlogFallback->isVisible()) {
    dispatchEvent(*backlogFallback, event);
    return true;
  }
  if (backlogModel.IsOpen()) {
    if (escape) {
      playSound(Ui::UiSfxCue::Cancel);
      if (!beginSystemPanelClose())
        closePanels();
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
      int x = -1, y = -1;
      if (pointerToBackLog(event.motion.x, event.motion.y, &x, &y))
        backlogModel.SetSelectionFromPointer(x, y);
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
               event.button.button == SDL_BUTTON_LEFT) {
      Kotonoha::SchoolDaysBackLogModel::Action action;
      int x = -1, y = -1;
      if (pointerToBackLog(event.button.x, event.button.y, &x, &y) &&
          backlogModel.ActivatePointer(x, y, &action) &&
          action.kind == Kotonoha::SchoolDaysBackLogModel::ActionKind::Close) {
        playSound(Ui::UiSfxCue::Click);
        if (!beginSystemPanelClose())
          closePanels();
      }
    }
    return true;
  }
  if (systemUi.IsOpen()) {
    if (systemUi.IsSaveLoadCommentEditorOpen()) {
      Ui::Output output;
      try {
        if (systemUi.HandleEvent(&engine->GetRenderer(), &event, &output))
          handlePanel(output);
      } catch (const std::exception &error) {
        setStatus(error.what());
      }
      return true;
    }
    if (escape) {
      if (bridge.HasPendingHandoff() &&
          (handoffUiState == HandoffUiState::SaveFeel ||
           handoffUiState == HandoffUiState::SaveLoad) &&
          (systemUi.IsSaveFeelOpen() || systemUi.IsSaveLoadOpen())) {
        return true;
      }
      playSound(Ui::UiSfxCue::Cancel);
      if (systemUi.IsRouteMapOpen()) {
        const bool returningToSave = routeMapReturnSave.value_or(false);
        const int selection = routeMapReturnSelection;
        openSaveLoad(returningToSave, false);
        systemUi.RestoreSaveLoadSelection(selection);
        routeMapReturnSave.reset();
      } else if ((frontendChild == FrontendChild::Load &&
                  systemUi.IsSaveLoadOpen()) ||
                 (frontendChild == FrontendChild::Config &&
                  systemUi.IsConfigOpen()) ||
                 (frontendChild == FrontendChild::Replay &&
                  systemUi.IsReplayOpen()))
        beginChildToTitle(SDL_GetTicks());
      else if (!beginSystemPanelClose())
        closePanels();
      return true;
    }
    Ui::Output output;
    try {
      if (systemUi.HandleEvent(&engine->GetRenderer(), &event, &output))
        handlePanel(output);
    } catch (const std::exception &error) {
      setStatus(error.what());
    }
    return true;
  }
  if (modal())
    return true;
  try {
    if (currentState == Kotonoha::GameState::Gameplay &&
        gameScene->handleEvent(event))
      return true;
    if (currentState == Kotonoha::GameState::Menu && !titleArtwork) {
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_UP)
          titleSelection = (titleSelection + 3) % 4;
        if (event.key.key == SDLK_DOWN)
          titleSelection = (titleSelection + 1) % 4;
      }
      for (int i = 0; i < 4; ++i)
        if (Kotonoha::UI::clicked(event, {400, 250.0f + i * 70, 480, 55}) ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_RETURN &&
             !event.key.repeat && titleSelection == i)) {
          if (i == 0)
            beginTitleHandoff(FrontendHandoff::TitleStart, SDL_GetTicks());
          if (i == 1)
            beginTitleHandoff(FrontendHandoff::TitleLoad, SDL_GetTicks());
          if (i == 2)
            beginTitleHandoff(FrontendHandoff::TitleConfig, SDL_GetTicks());
          if (i == 3)
            openExit();
          return true;
        }
    } else if (currentState == Kotonoha::GameState::Menu && title.IsOpen()) {
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        int x = -1, y = -1;
        if (pointerToTitle(event.motion.x, event.motion.y, &x, &y))
          title.SetSelectionFromPointer(x, y);
      }
      if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
          event.button.button == SDL_BUTTON_LEFT) {
        int x = -1, y = -1;
        if (!pointerToTitle(event.button.x, event.button.y, &x, &y))
          return true;
        title.SetSelectionFromPointer(x, y);
        Kotonoha::SchoolDaysTitleModel::Activation action;
        if (title.ActivateSelection(&action)) {
          using Result = Kotonoha::SchoolDaysTitleModel::Result;
          if (action.result != Result::Exit)
            playSound(Ui::UiSfxCue::Click);
          if (action.result == Result::Start)
            beginTitleHandoff(FrontendHandoff::TitleStart, SDL_GetTicks());
          else if (action.result == Result::Load)
            beginTitleHandoff(FrontendHandoff::TitleLoad, SDL_GetTicks());
          else if (action.result == Result::Config)
            beginTitleHandoff(FrontendHandoff::TitleConfig, SDL_GetTicks());
          else if (action.result == Result::Replay)
            beginTitleHandoff(FrontendHandoff::TitleReplay, SDL_GetTicks());
          else if (action.result == Result::Exit)
            openExit();
        }
      }
    }
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
      return true;
    if (event.key.key == SDLK_ESCAPE) {
      if (currentState == Kotonoha::GameState::Menu)
        beginTitleHandoff(FrontendHandoff::TitleConfig, SDL_GetTicks());
      else
        openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def);
      return true;
    }
    if (currentState != Kotonoha::GameState::Gameplay)
      return true;
    if (event.key.key == SDLK_SPACE)
      paused = !paused;
    if (!temporaryPlaybackActive() && event.key.key == SDLK_F5)
      saveGame((documents / "QuickSave").string());
    if (!temporaryPlaybackActive() && event.key.key == SDLK_F6)
      loadGame((documents / "QuickSave").string());
    if (event.key.key == SDLK_LEFT)
      back();
    if (event.key.key == SDLK_RIGHT)
      if (auto target = timeline.ComputeSkipToEndTarget(tick))
        seek(*target);
  } catch (const std::exception &error) {
    setStatus(error.what());
    return true;
  }
  return false;
}

void Runtime::activateControl(int control) {
  const bool replay = replayPlayback && replayPlayback->IsOpen();
  const bool playData = playDataPlayback && playDataPlayback->IsOpen();
  if (replay || playData) {
    if (control == 1)
      paused = !paused;
    else if (control == 2)
      back();
    else if (control == 3) {
      if (auto target = timeline.ComputeSkipToEndTarget(tick))
        seek(*target);
    } else if (playData && (control == 10 || control == 11)) {
      openSaveLoad(control == 10);
    } else if (playData && control == 12) {
      openBacklog();
    } else if (control == 13) {
      openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def);
    } else if (control == 14) {
      finishTemporaryPlayback(false);
    } else {
      setStatus("This control is unavailable during Replay playback");
    }
    return;
  }
  if (control == 1)
    paused = !paused;
  else if (control == 2)
    back();
  else if (control == 3) {
    if (auto target = timeline.ComputeSkipToEndTarget(tick))
      seek(*target);
  } else if (control == 4)
    skipToNextChoice();
  else if (control == 10 || control == 11) {
    openSaveLoad(control == 10 &&
                 currentState == Kotonoha::GameState::Gameplay);
  } else if (control == 12)
    openBacklog();
  else if (control == 13)
    openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Def);
  else if (control == 15)
    openRouteMap();
  else if (control == 16)
    openExit();
  else if (control == 14)
    openMainMenuConfirm();
}

Runtime::MenuBarProjection Runtime::menuBarProjection() const {
  const bool replay = replayPlayback && replayPlayback->IsOpen();
  const bool playData = playDataPlayback && playDataPlayback->IsOpen();
  const bool temporary = replay || playData;
  MenuBarProjection projection;
  projection.actionInhibit = replay;
  projection.fullBarSuppressed = temporary ? false : bridge.HasPendingHandoff();
  projection.replayIndicator = playData;
  return projection;
}

void Runtime::updateMenuBar(Kotonoha::SchoolDaysMenuBarPresenter &presenter,
                            uint32_t nowMs) {
  const bool temporary = temporaryPlaybackActive();
  const MenuBarProjection projection = menuBarProjection();

  // Ordinary HScene replay owns the authenticated action-inhibit path. Replay
  // Data keeps Save/Load/BackLog live and owns the replay-indicator controls.
  // A retained narrative handoff must not suppress a temporary playback owner.
  presenter.SetRuntimeState(
      config.autoMode, paused, speedIndex, config.superSkip,
      projection.speedControlsEnabled, projection.transportLocked,
      projection.actionInhibit, projection.fullBarSuppressed);
  // Native choice-result tail removes transport/system controls while the
  // KOTONOHA/SEKAI feelings meter remains visible at full opacity. Normal
  // MenuBar ownership resumes when the next scene is bound.
  presenter.SetMeterOnly(!temporary && choicePresentation.exitActive);
  presenter.SetReplayIndicatorState(projection.replayIndicator);

  int64_t feeling001 = 0;
  int64_t feeling002 = 0;
  if (!temporary) {
    if (bridge.Session().ConsumeTopMeterRefreshRequest())
      presenter.RequestFeelingRefresh();
    if (presenter.FeelingLatch()) {
      Kotonoha_KtrfError error{};
      Check(bridge.Session().State().ReadSessionInt("001", &feeling001, &error),
            error);
      Check(bridge.Session().State().ReadSessionInt("002", &feeling002, &error),
            error);
      if (feeling001 < std::numeric_limits<int>::min() ||
          feeling001 > std::numeric_limits<int>::max() ||
          feeling002 < std::numeric_limits<int>::min() ||
          feeling002 > std::numeric_limits<int>::max())
        throw std::runtime_error(
            "School Days top-meter feeling value is out of range");
    }
  }
  presenter.Update(nowMs, static_cast<int>(feeling001),
                   static_cast<int>(feeling002));
  // Retail menu-bar feeling animation calls host sound code 3=SeUp or
  // 4=SeDown exactly when its first changed feeling is detected. SeSelect
  // remains the immediate choice click; the ~4s directional musical effect
  // belongs to the later animated meter, not to every choice unconditionally.
  const int feelingSfx = presenter.ConsumeFeelingDirectionCode();
  if (feelingSfx == 3)
    playSound(Ui::UiSfxCue::Up);
  else if (feelingSfx == 4)
    playSound(Ui::UiSfxCue::Down);
}

void Runtime::applyEffectivePlaybackRate(double rate) {
  speed = rate;
  for (auto &item : playing)
    item.player->setPlaybackRate(rate);
  if (!engine)
    return;
  // EXE 0x4433D0 compares effective rate >4.0; gain-based silencing keeps
  // FFmpeg/video decoding and the 24-fps logical clock alive. These four
  // channels are School Days' native story audio and authored video sound.
  const bool mute =
      Kotonoha::SchoolDaysPlaybackPolicy::SuppressesNarrativeAudioAtRate(rate);
  for (const char *channel : {"BGM", "Voice", "SE", "Movie"})
    engine->GetAudio().channel(channel)->setMuted(mute);
}

void Runtime::setPlaybackSpeedIndex(int index) {
  double rate = 1.0;
  if (!Kotonoha::SchoolDaysPlaybackPolicy::RateForIndex(index, &rate))
    throw std::runtime_error(
        "School Days MenuBar playback speed index is invalid");
  speedIndex = index;
  if (!choiceSpeedOverrideActive)
    applyEffectivePlaybackRate(rate);
}

void Runtime::activateMenuAction(
    const Kotonoha::SchoolDaysMenuBarPresenter::Action &action) {
  using Kind = Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
  const bool temporary = temporaryPlaybackActive();
  const bool playData = playDataPlayback && playDataPlayback->IsOpen();

  switch (action.kind) {
  case Kind::None:
    return;
  case Kind::ToggleAutoMode:
    config.autoMode = !config.autoMode;
    choiceInteraction.SetAutoMode(config.autoMode);
    cacheConfigState();
    return;
  case Kind::TogglePause:
    paused = !paused;
    return;
  case Kind::BackOrRestartScene:
    beginTransportFade(Kotonoha::SchoolDaysTransportFade::Operation::Rewind);
    return;
  case Kind::SkipToEnd:
    beginTransportFade(Kotonoha::SchoolDaysTransportFade::Operation::SkipToEnd);
    return;
  case Kind::SkipToNextChoice:
    if (!temporary)
      skipToNextChoice();
    return;
  case Kind::SetPlaybackSpeed: {
    setPlaybackSpeedIndex(action.value);
    return;
  }
  case Kind::SetReplayIndicatorOpacity:
    // Presenter owns the authenticated local selector/alpha. It is exposed only
    // with the proven PlayData +0x98 latch and must not mutate narrative state.
    if (!playData)
      setStatus("Replay opacity is unavailable outside Replay Data");
    return;
  case Kind::OpenSave:
  case Kind::OpenLoad:
  case Kind::OpenBackLog:
    if (replayPlayback && replayPlayback->IsOpen()) {
      setStatus("Save/Load/BackLog is unavailable during HScene Replay");
      return;
    }
    playSound(Ui::UiSfxCue::Click);
    if (action.kind == Kind::OpenSave)
      beginSystemPanel(Kotonoha::SchoolDaysSystemPanelTransition::Target::Save);
    else if (action.kind == Kind::OpenLoad)
      beginSystemPanel(Kotonoha::SchoolDaysSystemPanelTransition::Target::Load);
    else
      beginSystemPanel(
          Kotonoha::SchoolDaysSystemPanelTransition::Target::BackLog);
    return;
  case Kind::OpenConfig:
    playSound(Ui::UiSfxCue::Click);
    beginSystemPanel(Kotonoha::SchoolDaysSystemPanelTransition::Target::Config);
    return;
  case Kind::OpenMainMenuConfirm:
    openMainMenuConfirm();
    return;
  }
}

void Runtime::openSaveLoad(bool save, bool playView) {
  traceDev(save ? "SaveLoadOpen:Save" : "SaveLoadOpen:Load");
  if (replayPlayback && replayPlayback->IsOpen()) {
    setStatus("Save/Load is unavailable during HScene Replay");
    return;
  }
  const SaveLoadOwner owner = playDataPlayback && playDataPlayback->IsOpen()
                                  ? SaveLoadOwner::PlayData
                                  : SaveLoadOwner::Narrative;
  const bool handoffSave = save && bridge.HasPendingHandoff() &&
                           handoffUiState == HandoffUiState::SaveLoad;
  closePanels();
  saveLoadSaving = save;
  std::string failure;
  if (storage.IsInitialized() &&
      systemUi.OpenSaveLoad(
          &engine->GetRenderer(), assets->root().string(), uiVariant,
          save ? Kotonoha::SchoolDaysSaveLoadModel::Mode::Save
               : Kotonoha::SchoolDaysSaveLoadModel::Mode::Load,
          profile.textInput, profile.useEnglish, profile.fontData, &failure)) {
    saveLoadOwner = owner;
    if (playView)
      playSound(Ui::UiSfxCue::View);
    refreshSaveMetadata();
    return;
  }
  if (!failure.empty())
    SDL_LogWarn(Kotonoha::LogUI, "School Days SaveLoad: %s", failure.c_str());
  if (handoffSave)
    throw std::runtime_error(
        failure.empty()
            ? "School Days handoff SaveLoad is unavailable"
            : "School Days handoff SaveLoad open failed: " + failure);
  if (owner == SaveLoadOwner::PlayData) {
    setStatus(failure.empty()
                  ? "Replay Data Save/Load is unavailable"
                  : "Replay Data Save/Load open failed: " + failure);
    return;
  }
  const auto directory = documents / "Saves";
  std::filesystem::create_directories(directory);
  saveMenu->open(directory.string(), !save);
}

void Runtime::refreshSaveMetadata() {
  if (!systemUi.IsSaveLoadOpen())
    return;
  const int page = systemUi.SaveLoad().Page();
  std::vector<Kotonoha::SchoolDaysSaveService::SlotMetadata> rows(10u);
  for (int row = 0; row < 10; ++row) {
    bool openable = false;
    std::string failure;
    Kotonoha_KtrfError error{};
    // One unreadable slot must not hide the rest of the page.
    if (!storage.ProbeSaveFile(page * 10 + row, &openable, &failure) ||
        !Kotonoha::SchoolDaysSaveService::ReadSlotMetadata(
            profile, page * 10 + row, openable, bridge.Session().Persistence(),
            &rows[size_t(row)], &error))
      rows[size_t(row)] = {};
  }
  std::string failure;
  if (!systemUi.SetSaveLoadMetadata(rows, &failure))
    SDL_LogWarn(Kotonoha::LogUI, "School Days save metadata: %s",
                failure.c_str());
  saveLoadPage = page;
}

void Runtime::saveSlot(int slot, const std::u16string &comment) {
  if (currentState != Kotonoha::GameState::Gameplay)
    throw std::runtime_error("No game to save");
  const bool privatePlayData = saveLoadOwner == SaveLoadOwner::PlayData;
  if (privatePlayData &&
      (!playDataPlayback || !playDataPlayback->IsOpen() || !saveLoadSaving))
    throw std::runtime_error("Replay Data SAVE owner is unavailable");
  auto &session =
      privatePlayData ? playDataPlayback->Session() : bridge.Session();
  SDL_Time now = 0;
  SDL_DateTime date{};
  if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &date, true))
    throw std::runtime_error(SDL_GetError());
  const Kotonoha::SchoolDaysSaveMetadata::LocalTime time{
      date.year,        date.month, date.day,
      date.day_of_week, date.hour,  date.minute};
  Kotonoha::SchoolDaysSaveMetadata::Result metadata;
  Kotonoha_KtrfError error{};
  Check(Kotonoha::SchoolDaysSaveMetadata::Build(session.State(), time,
                                                profile.useEnglish, comment,
                                                &metadata, &error),
        error);
  Kotonoha::SchoolDaysSaveService::Plan plan;
  Check(Kotonoha::SchoolDaysSaveService::PlanSlot(
            profile, slot, metadata.primary, metadata.sub,
            session.Persistence(), session.State(), &plan, &error),
        error);
  if (!privatePlayData && bridge.HasPendingHandoff()) {
    // A pending handoff keeps the script snapshot taken at the scene boundary.
    plan.saveFileBytes.clear();
    Check(session.Persistence().SerializeSaveLogWithLiveChoiceCommitMarker(
              false, &plan.saveFileBytes, &error),
          error);
  }
  std::string failure;
  Check(storage.WriteSaveFile(plan, &failure), failure,
        "School Days save write failed");
  Check(Kotonoha::SchoolDaysSaveService::Commit(plan, &session.Persistence(),
                                                &error),
        error);
  Check(storage.WriteGlobalFlag(plan, &failure), failure,
        "School Days GlobalFlag write failed");
  if (privatePlayData) {
    // Retail has one GlobalFlag owner. Keep the private PlayData route/session,
    // history and SLog isolated, and publish only the committed global image.
    Check(bridge.Session().LoadGlobalFlag(plan.globalFlagBytes, &error), error);
  }
}

void Runtime::loadSlot(int slot) {
  std::vector<uint8_t> bytes;
  std::string failure;
  Check(storage.ReadSaveFile(slot, &bytes, &failure), failure,
        "School Days save read failed");
  if (saveLoadOwner == SaveLoadOwner::PlayData) {
    if (!playDataPlayback || !playDataPlayback->IsOpen() || saveLoadSaving)
      throw std::runtime_error("Replay Data LOAD owner is unavailable");
    Kotonoha_KtrfError error{};
    if (playDataPlayback->ChoiceReplayLatchArmed()) {
      Check(playDataPlayback->ReplaceRecordedChoiceOracle(bytes, &error),
            error);
      closePanels();
      return;
    }

    const bool userPaused = paused;
    retireSceneEventPointers();
    Check(playDataPlayback->RestartFromRecordedChoiceOracle(bytes, &error),
          error);
    Check(
        playDataPlayback->Session().Persistence().ReplaceCurrentScriptSnapshot(
            playDataPlayback->CurrentScene().sceneKey, UINT32_C(0x3f800000),
            playDataPlayback->Session().State(), &error),
        error);
    closePanels();
    playDataBacklogStore.ResetTransient();
    playDataBacklogIndex = 0;
    playDataBacklogScene.clear();
    bindScene(playDataPlayback->CurrentScene(),
              playDataPlayback->CurrentGameplay());
    paused = userPaused;
    return;
  }
  prepareCarryFrame();
  retireSceneEventPointers();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.LoadSave(bytes, &result, &error), error);
  // A successful Title->Load selection leaves the frontend child hierarchy and
  // enters narrative Gameplay. Do not let a later gameplay SaveLoad inherit the
  // stale child->Title close policy.
  frontendChild = FrontendChild::None;
  closePanels();
  backlogStore.ResetTransient();
  if (backlogFallback)
    backlogFallback->clear();
  backlogIndex = 0;
  backlogScene.clear();
  openScene(result.scene);
}

Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState
Runtime::configVisualState() const {
  Kotonoha::SchoolDaysConfigMenuRenderPlan::VisualState state;
  state.defWideCurrent = displayState.effectiveDisplayType == 1 ? 4 : 5;
  state.defFullCurrent = displayBackend.windowMode == 0 ? 6 : 7;
  state.defSkipCurrent = config.skip ? 8 : 9;
  state.defSuperSkipCurrent = config.superSkip ? 10 : 11;
  state.defDrawMessageCurrent = config.textView ? 12 : 13;
  state.soundMenVoiceCurrent = config.menVoice ? 10 : 11;
  state.soundMuteCurrent = engine->GetSettings().masterMuted ? 12 : 13;
  state.soundLevels = {Policy::Level(config, Policy::NativeAudioBank::Bgm),
                       Policy::Level(config, Policy::NativeAudioBank::Se),
                       Policy::Level(config, Policy::NativeAudioBank::Voice)};
#if !defined(__ANDROID__) && !defined(ANDROID)
  if (engine) {
    const auto &display =
        desktopDisplayDraftActive ? desktopDisplayDraft : engine->GetSettings();
    state.displayResizable = display.resizable ? 5 : 6;
    state.displayVsync = display.vsync ? 7 : 8;
    state.displayFactor =
        13 + std::clamp(static_cast<int>(std::lround(
                            (display.upscaling.factor - 1.0f) * 2.0f)),
                        0, 6);
  }
#endif
  return state;
}

void Runtime::openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page page,
                         bool playView) {
#if defined(_WIN32) && !defined(__ANDROID__)
  if (engine) {
    // The shared SettingsMenu may have changed the host's physical window
    // mode while the School Days native panel was closed. Reflect it in the
    // next General WINDOW/FULL chip without clobbering any saved Config.DAT
    // field simply by opening the panel.
    const int physicalWindowMode =
        engine->GetSettings().mode == Kotonoha::WindowMode::Windowed ? 0 : 1;
    if (displayBackend.windowMode != physicalWindowMode) {
      displayBackend.windowMode = physicalWindowMode;
      refreshDisplayProjection();
    }
  }
#endif
  closePanels();
  std::string failure;
  if (systemUi.OpenConfig(&engine->GetRenderer(), assets->root().string(),
                          ConfigVariant(uiVariant), page, configVisualState(),
                          &failure)) {
#if !defined(__ANDROID__) && !defined(ANDROID)
    if (page == Kotonoha::SchoolDaysConfigMenuModel::Page::Display) {
      resetDesktopDisplayDraft();
      systemUi.SetConfigVisualState(configVisualState());
    }
#endif
    if (playView)
      playSound(Ui::UiSfxCue::View);
    return;
  }
  SDL_LogWarn(Kotonoha::LogUI, "School Days ConfigMenu: %s", failure.c_str());
  // School Days no longer opens the engine-wide SettingsMenu, including when
  // a required authored texture/CMAP cannot be resolved. Fail visibly rather
  // than silently replacing the native three-tab menu with a global UI.
  setStatus("School Days ConfigMenu unavailable: " + failure);
}

void Runtime::handleConfig(
    const Kotonoha::SchoolDaysConfigMenuModel::Action &action) {
  using Kind = Kotonoha::SchoolDaysConfigMenuModel::ActionKind;
  const auto before = config;
  const auto beforeVariant = uiVariant;
  const auto currentPage = systemUi.IsConfigOpen()
                               ? systemUi.ConfigMenu().CurrentPage()
                               : Kotonoha::SchoolDaysConfigMenuModel::Page::Def;
  bool displayAccepted = false;
  switch (action.kind) {
  case Kind::Close:
    if (frontendChild == FrontendChild::Config)
      beginChildToTitle(SDL_GetTicks());
    else if (!beginSystemPanelClose())
      closePanels();
    return;
  case Kind::SelectPage:
    if (action.page == Kotonoha::SchoolDaysConfigMenuModel::Page::Display) {
#if defined(__ANDROID__) || defined(ANDROID)
      // The mobile-specific render-scale/frame-rate panel remains available;
      // mobile modes have a different platform presentation contract.
      openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Display, false);
#else
      // User-selected third tab now owns its retail-style Display base/CHIP.
      // Its initial draft is a copy of the engine's persisted settings.
      openConfig(Kotonoha::SchoolDaysConfigMenuModel::Page::Display, false);
#endif
      displayPanelResolutionOpen = false;
      displayPanelPresetOpen = false;
      displayPanelHoverRow = -1;
      displayPanelHoverResolution = -1;
      displayPanelHoverPreset = -1;
      return;
    }
    openConfig(action.page, false);
    displayPanelResolutionOpen = false;
    displayPanelPresetOpen = false;
    displayPanelHoverRow = -1;
    displayPanelHoverResolution = -1;
    displayPanelHoverPreset = -1;
    return;
#if !defined(__ANDROID__) && !defined(ANDROID)
  case Kind::DisplayControl:
    if (currentPage == Kotonoha::SchoolDaysConfigMenuModel::Page::Display)
      activateDesktopDisplayControl(action.value);
    return;
#endif
  case Kind::SetMenVoice:
    config.menVoice = action.value != 0;
    break;
  case Kind::SetSkip: {
    const bool requestedSkip = action.value != 0;
    if (!requestedSkip) {
      setPlaybackSpeedIndex(0);
    }
    config.skip = requestedSkip;
    break;
  }
  case Kind::SetSuperSkip:
    config.superSkip = action.value != 0;
    break;
  case Kind::SetDrawMessage:
    config.textView = action.value != 0;
    break;
  case Kind::SetMute:
    config.mute = action.value != 0;
    break;
  case Kind::StepSoundBank:
  case Kind::SetSoundLevel: {
    if (action.soundBank < 0 || action.soundBank > 2)
      return;
    const auto bank = static_cast<Policy::UiAudioBank>(action.soundBank);
    if (action.kind == Kind::StepSoundBank)
      Policy::StepUiLevel(&config, bank, action.choice);
    else
      Policy::SetUiLevel(&config, bank, action.value);
    break;
  }
  case Kind::RequestWide: {
    displayAccepted = DisplayPolicy::ApplyRetailConfigRequest(
        DisplayPolicy::RetailConfigRequest::Wide, action.value, {0, 1, 1},
        &displayBackend);
    if (displayAccepted) {
      refreshDisplayProjection();
      std::string failure;
      Check(configStorage.CacheDisplayType(displayBackend.baseDisplayType,
                                           &failure),
            failure, "School Days DisplayType cache update failed");
    }
    // A click on an already-selected WIDE/STANDARD chip is inert for retail
    // B/D, but must still turn Display's "Auto" aspect into the explicit
    // player selection. Both tabs represent one remaster preference.
    if (displayState.effectiveDisplayType == (action.value != 0 ? 1 : 0)) {
      const int requested = action.value != 0 ? 2 : 1;
      if (displayAspectPreference != requested) {
        displayAspectPreference = requested;
        SaveRemasterAspectPreference(documents, requested);
      }
    }
    break;
  }
  case Kind::RequestFull: {
    // General WINDOW/FULL must also change the real Windows presentation.
    // Preserve the retail W/skin transition and use the same physical mode
    // state as Display Options; previously this was a no-op for the user.
#if defined(_WIN32) && !defined(__ANDROID__)
    {
      auto settings = engine->GetSettings();
      const SDL_DisplayID display = DisplayIdForIndex(settings.display);
      const SDL_DisplayMode *desktop =
          display ? SDL_GetDesktopDisplayMode(display) : nullptr;
      if (Kotonoha::SchoolDaysDesktopStartupProfile::SelectGeneralWindowSize(
              settings, action.value == 0, desktop ? desktop->w : 0,
              desktop ? desktop->h : 0,
              desktop ? desktop->refresh_rate : 0.0f)) {
        engine->applySettings(settings);
        displayPanelSelectedResolution = -1;
      }
      // A normal decorated SDL window is required when WINDOW is selected.
      // Explicitly restore native borders after leaving fullscreen to avoid
      // preserving a borderless Windows style across mode changes. A desktop-
      // sized *client* rectangle plus the caption extends above/below the
      // display if merely centered (observed HWND y=-22 on 1366x768). Native
      // maximization keeps the title bar and Min/Max/Close visible, while the
      // client fills the monitor's usable work area instead of clipping it.
      // SDL dummy/offscreen test backends have no actual Win32 caption or
      // taskbar to maximize into. Only dispatch OS window-style requests when
      // the active SDL video backend is really Windows; the EngineSettings
      // transition itself is still exercised in headless regression tests.
      const char *videoDriver = SDL_GetCurrentVideoDriver();
      if (action.value == 1 && videoDriver &&
          SDL_strcmp(videoDriver, "windows") == 0) {
        if (!SDL_SetWindowBordered(&engine->GetWindow(), true))
          throw std::runtime_error(std::string("Restore Windows title bar: ") +
                                   SDL_GetError());
        if (!SDL_MaximizeWindow(&engine->GetWindow()))
          throw std::runtime_error(std::string("Maximize desktop window: ") +
                                   SDL_GetError());
      }
    }
#endif
    displayAccepted = DisplayPolicy::ApplyRetailConfigRequest(
        DisplayPolicy::RetailConfigRequest::Full, action.value, {0, 1, 1},
        &displayBackend);
    if (displayAccepted) {
      refreshDisplayProjection();
    }
    // The v1.02 Config.DAT reader understands W but no writer was
    // authenticated. The remaster adds this explicit persistence only on the
    // user's choice.
    std::string failure;
    Check(configStorage.CacheWindowMode(displayBackend.windowMode, &failure),
          failure, "School Days WindowMode cache update failed");
    break;
  }
  default:
    break;
  }
  applyConfig();
  const bool stateChanged =
      before.voiceVolume != config.voiceVolume ||
      before.bgmVolume != config.bgmVolume ||
      before.seVolume != config.seVolume ||
      before.textView != config.textView ||
      before.menVoice != config.menVoice || before.mute != config.mute ||
      before.skip != config.skip || before.autoDraw != config.autoDraw ||
      before.autoMode != config.autoMode ||
      before.superSkip != config.superSkip;
  if (stateChanged)
    cacheConfigState();
  if (displayAccepted && uiVariant != beforeVariant &&
      systemUi.IsConfigOpen()) {
    // Rebind the authored CMAP/skin immediately, but the action itself already
    // emitted its UI cue. Do not synthesize a second View sound on refresh.
    openConfig(currentPage, false);
  } else {
    systemUi.SetConfigVisualState(configVisualState());
  }
}

void Runtime::applyConfig() {
  if (!engine)
    return;
  auto settings = engine->GetSettings();
  if (settings.masterMuted != config.mute) {
    settings.masterMuted = config.mute;
    engine->applySettings(settings);
  }
  auto &audio = engine->GetAudio();
  audio.channel("BGM")->setVolume(
      Policy::LinearGain(config, Policy::NativeAudioBank::Bgm));
  audio.channel("SE")->setVolume(
      Policy::LinearGain(config, Policy::NativeAudioBank::Se));
  audio.channel("Voice")->setVolume(
      Policy::LinearGain(config, Policy::NativeAudioBank::Voice));
}

void Runtime::loadConfig() {
  std::string failure;
  Kotonoha::SchoolDaysConfigStorage::LoadResult loaded;
  Check(configStorage.Initialize(documents, &failure), failure,
        "School Days Config.DAT initialization failed");
  Check(configStorage.Load(&loaded, &failure), failure,
        "School Days Config.DAT load failed");
  config = loaded.state;

  int outputWidth = 1280;
  int outputHeight = 720;
  if (engine) {
    int queriedWidth = 0;
    int queriedHeight = 0;
    if (SDL_GetRenderOutputSize(&engine->GetRenderer(), &queriedWidth,
                                &queriedHeight) &&
        queriedWidth > 0 && queriedHeight > 0) {
      outputWidth = queriedWidth;
      outputHeight = queriedHeight;
    }
  }
  DisplayPolicy::Decision automatic;
  Check(DisplayPolicy::SelectAuto(outputWidth, outputHeight, &automatic),
        "School Days display auto profile selection failed",
        "School Days display auto profile selection failed");
  const auto canonical =
      DisplayPolicy::CanonicalRetailStateForVariant(automatic.variant);
  displayBackend.baseDisplayType = canonical.effectiveDisplayType;
  displayBackend.deviceDisplayType = canonical.effectiveDisplayType;
  displayBackend.windowMode = canonical.windowMode;
  displayBackend.typeMiniNote = canonical.typeMiniNote;

  // v3 has no retail DX-device table. Persisted DisplayType therefore seeds B
  // and D equally as an explicit host adapter policy; W/M keep their native
  // Config.DAT meanings. This projection is done once and never on resize.
  if (loaded.display.hasDisplayType) {
    displayBackend.baseDisplayType = loaded.display.displayType;
    displayBackend.deviceDisplayType = loaded.display.displayType;
  }
  if (loaded.display.hasWindowMode)
    displayBackend.windowMode = loaded.display.windowMode;
  if (loaded.display.hasTypeMiniNote)
    displayBackend.typeMiniNote = loaded.display.typeMiniNote;

  // On Windows the stored physical mode takes precedence over retail W.
  // Elsewhere General Full changes only the retail projection; a persisted
  // host Windowed default must not erase the user's Config.DAT selection.
  if (engine && !loaded.display.hasWindowMode)
    displayBackend.windowMode =
        engine->GetSettings().mode == Kotonoha::WindowMode::Windowed ? 0 : 1;
#if defined(_WIN32) && !defined(__ANDROID__)
  else if (engine && engine->HasStoredSettings())
    displayBackend.windowMode =
        engine->GetSettings().mode == Kotonoha::WindowMode::Windowed ? 0 : 1;
#endif
  refreshDisplayProjection();

  displayAspectPreference = LoadRemasterAspectPreference(documents);
  if (displayAspectPreference != 0)
    Check(applyDisplayAspectPreference(),
          "School Days remaster aspect preference failed",
          "School Days remaster aspect preference failed");
}

void Runtime::refreshDisplayProjection() {
  displayState = DisplayPolicy::ProjectRetailState(displayBackend);
  uiVariant = DisplayPolicy::SelectRetailVariant(displayState);
}

float Runtime::narrativePresentationHeightScale() const {
#if defined(_WIN32) && !defined(__ANDROID__)
  // In the supplied 800x600 Standard screenshot, the 800x450 picture is
  // centered with 75px bars above and below; WIDE restores full height. Keep
  // the actual Windows fullscreen resolution unchanged and scale the complete
  // game-authored Novel frame, never crop media or subtitle glyphs.
  return engine &&
                 engine->GetSettings().mode != Kotonoha::WindowMode::Windowed &&
                 uiVariant == DisplayPolicy::Variant::Standard
             ? 0.75f
             : 1.0f;
#else
  return 1.0f;
#endif
}

void Runtime::cacheConfigState() {
  std::string failure;
  Check(configStorage.CacheState(config, &failure), failure,
        "School Days Config.DAT cache update failed");
}

void Runtime::openRouteMap(bool playView) {
  const bool fromSaveLoad = systemUi.IsSaveLoadOpen();
  routeMapReturnSave =
      fromSaveLoad ? std::optional<bool>(saveLoadSaving) : std::nullopt;
  routeMapReturnSelection = fromSaveLoad ? systemUi.SaveLoad().Selection() : -1;
  closePanels();
  auto &state = bridge.Session().State();
  Kotonoha_KtrfError error{};
  int64_t route = 0, endClear = 0;
  int major = 0, subpage = 0, percent = 0;
  if (state.ReadSessionInt("ROUTE", &route, &error) &&
      state.ReadGlobalInt("EndClear", &endClear, &error)) {
    // Native RouteMap init: GetStory()-1, its page, then the -1 -> 0 clamp.
    const int raw =
        Kotonoha::SchoolDaysSaveMetadata::StoryFor(route, endClear) - 1;
    Kotonoha_KtrfError pageError{};
    subpage = Kotonoha::SchoolDaysRouteMapOracle::GetRouteMapPage(state, raw,
                                                                  &pageError);
    major = std::clamp(raw, 0, 5);
    if (pageError.code != KOTONOHA_KTRF_OK || subpage < 0 ||
        subpage >=
            Kotonoha::SchoolDaysRouteMapModel::SubpageCountForMajor(major))
      subpage = 0;
  }
  Kotonoha::SchoolDaysRouteMapOracle::Completion completion;
  if (Kotonoha::SchoolDaysRouteMapOracle::ComputeCompletion(
          state, bridge.Session().Adapter()->document, &completion, &error))
    percent = completion.percent;
  std::string failure;
  if (!systemUi.OpenRouteMap(&engine->GetRenderer(), assets->root().string(),
                             uiVariant, major, subpage, false, percent,
                             profile.useEnglish, profile.fontData, &failure)) {
    setStatus("Route map unavailable: " + failure);
    if (fromSaveLoad) {
      openSaveLoad(*routeMapReturnSave);
      systemUi.RestoreSaveLoadSelection(routeMapReturnSelection);
    }
    routeMapReturnSave.reset();
    return;
  }
  if (playView)
    playSound(Ui::UiSfxCue::View);
  refreshRouteMap();
}

void Runtime::refreshRouteMap() {
  if (!systemUi.IsRouteMapOpen())
    return;
  const int major = systemUi.RouteMap().Major(),
            subpage = systemUi.RouteMap().Subpage();
  const auto page = Kotonoha::SchoolDaysRouteMapModel::Page(major, subpage);
  const auto &state = bridge.Session().State();
  const auto &persistence = bridge.Session().Persistence();
  for (int i = 0; i < page.nodeCount; ++i) {
    const auto key =
        Kotonoha::SchoolDaysRouteMapModel::NodeSpKey(major, subpage, i);
    // RouteMap's unlocked nodes reflect the persisted global checkpoints.
    // KTRF global defaults can also be nonzero on a fresh profile, so the
    // sparse GlobalFlag document is the authoritative unlock source.
    systemUi.SetRouteMapNodeAvailable(i, persistence.GlobalIntegerIsSet(key));
  }
  int marker = -1;
  Kotonoha_KtrfError error{};
  // LOAD from the title has no active narrative cursor to highlight.
  if (currentState == Kotonoha::GameState::Gameplay &&
      Kotonoha::SchoolDaysRouteMapOracle::GetRouteMapPage(state, major,
                                                          &error) == subpage &&
      error.code == KOTONOHA_KTRF_OK) {
    const int check =
        Kotonoha::SchoolDaysRouteMapOracle::CheckScript(state, major, &error);
    if (error.code == KOTONOHA_KTRF_OK)
      marker = check - page.nodeOffset;
  }
  systemUi.SetRouteMapCurrentMarker(marker);
  routeMapMajor = major;
  routeMapSubpage = subpage;
}

void Runtime::openExit() {
  playSound(Ui::UiSfxCue::Click);
  closePanels();
  exitTransition.Begin(Kotonoha::SchoolDaysExitTransition::Target::Quit,
                       SDL_GetTicks());
}

bool Runtime::handleNativeQuitRequest() {
  if (exitConfirmedByPlayer)
    return false;
  // The title/gameplay host owns EXIT; during a pre-title splash its state
  // machine is not ticking yet. Do not swallow WM_CLOSE and strand the window.
  if (currentState == Kotonoha::GameState::Splash || !engine)
    return false;
  // Duplicate WM_CLOSE/SDL_QUIT events must not reset the fade or bypass the
  // YES/NO question once it is visible.
  if (exitTransition.Active() || systemUi.IsExitOpen())
    return true;
  openExit();
  return true;
}

void Runtime::openMainMenuConfirm() {
  playSound(Ui::UiSfxCue::Click);
  closePanels();
  exitTransition.Begin(Kotonoha::SchoolDaysExitTransition::Target::Title,
                       SDL_GetTicks());
}

void Runtime::openExitDialog(bool returnToTitle) {
  std::string failure;
  if (systemUi.OpenExit(
          &engine->GetRenderer(), assets->root().string(),
          ExitVariant(uiVariant),
          returnToTitle ? Kotonoha::SchoolDaysExitViewModel::BaseArtwork::Title
                        : Kotonoha::SchoolDaysExitViewModel::BaseArtwork::Exit,
          &failure)) {
    return;
  }
  exitTransition.Reset();
  setStatus(std::string(returnToTitle ? "Main Menu" : "Exit") +
            " confirmation unavailable: " + failure);
}

void Runtime::finishMainMenuReturn() {
  if (temporaryPlaybackActive()) {
    finishTemporaryPlayback(false);
    return;
  }
  retireSceneEventPointers();
  moveSom.Reset();
  Kotonoha_KtrfError error{};
  Kotonoha::KtrfStateStore::Snapshot stateBefore;
  Check(bridge.Session().State().Capture(&stateBefore, &error), error);
  const auto persistenceBefore = bridge.Session().Persistence();
  Check(bridge.Session().Persistence().WriteGlobalInteger(
            "FLAG_LOGO", 11, -1, &bridge.Session().State(), &error),
        error);
  if (!bridge.ReleaseCurrentGameplayForFrontend(&error)) {
    Kotonoha_KtrfError ignored{};
    (void)bridge.Session().State().Restore(stateBefore, &ignored);
    bridge.Session().Persistence() = persistenceBefore;
    Check(false, error);
  }
  script = {};
  backlogStore.ResetTransient();
  if (backlogFallback)
    backlogFallback->clear();
  backlogIndex = 0;
  backlogScene.clear();
  flushGlobal();
  openTitle();
}

void Runtime::openBacklog(bool playView) {
  if (replayPlayback && replayPlayback->IsOpen()) {
    setStatus("BackLog is unavailable during HScene Replay");
    return;
  }
  const BackLogOwner owner = playDataPlayback && playDataPlayback->IsOpen()
                                 ? BackLogOwner::PlayData
                                 : BackLogOwner::Narrative;
  const auto &store =
      owner == BackLogOwner::PlayData ? playDataBacklogStore : backlogStore;
  closePanels();
  std::string failure;
  TtfFont font;
  try {
    font.font = TTF_OpenFont(
        Kotonoha::FileResolver::font(engine->GetSettings().fontPath)
            .string()
            .c_str(),
        Kotonoha::SchoolDaysSubtitleTypography::PointSize);
  } catch (const std::exception &error) {
    failure = error.what();
  }
  // The authenticated retail text layout is the English horizontal BackLog.
  if (profile.useEnglish && font.font &&
      backlogModel.Open(assets->root().string(), store,
                        Kotonoha::SchoolDaysBackLogModel::Orientation::Horizon,
                        BackLogVariant(uiVariant), &failure) &&
      backlogPresenter.Open(&engine->GetRenderer(), assets->root().string(),
                            backlogModel.CurrentOrientation(), true, font.font,
                            nullptr, &failure)) {
    backlogOwner = owner;
    if (playView)
      playSound(Ui::UiSfxCue::View);
    return;
  }
  if (!failure.empty())
    SDL_LogWarn(Kotonoha::LogUI, "School Days BackLog: %s", failure.c_str());
  backlogModel.Close();
  backlogPresenter.Close();
  if (owner == BackLogOwner::PlayData) {
    backlogOwner = BackLogOwner::None;
    setStatus(failure.empty() ? "Replay Data BackLog is unavailable"
                              : "Replay Data BackLog open failed: " + failure);
    return;
  }
  backlogOwner = BackLogOwner::Narrative;
  backlogFallback->open();
}

bool Runtime::handleBacklogAction() {
  openBacklog();
  return true;
}

void Runtime::handlePanel(const Ui::Output &output) {
  playSound(output.uiSfx);
  if (output.hasSaveFeelResult) {
    submitSaveFeel(output.saveFeelResult);
    return;
  }
  if (output.hasConfigAction)
    handleConfig(output.configAction);
  if (output.hasReplayAction) {
    using Kind = Kotonoha::SchoolDaysReplayModel::ActionKind;
    const auto &action = output.replayAction;
    if (action.kind == Kind::Close) {
      if (frontendChild == FrontendChild::Replay)
        beginChildToTitle(SDL_GetTicks());
      else
        closePanels();
    } else if (action.kind == Kind::SceneCheckRequested) {
      (void)refreshReplayAvailability();
    } else if (action.kind == Kind::HSceneRequested) {
      beginReplayHScene(action.index);
    } else if (action.kind == Kind::SceneCheckAccepted) {
      beginReplayHScene(action.index, action.choice);
    } else if (action.kind == Kind::PlayDataRequested) {
      if (!loadTransition.BeginReplayData(action.index, SDL_GetTicks()))
        setStatus("School Days Replay Data loading is already in progress");
    }
  }
  if (output.hasExitActivation) {
    const bool confirm =
        output.exitActivation.outcome ==
        Kotonoha::SchoolDaysExitViewModel::Outcome::ConfirmExit;
    if (confirm) {
      exitTransition.Confirm(SDL_GetTicks());
    } else {
      closePanels();
      exitTransition.Cancel(SDL_GetTicks());
    }
    return;
  }
  if (output.hasRouteMapAction) {
    using Kind = Kotonoha::SchoolDaysRouteMapModel::ActionKind;
    if (output.routeMapAction.kind == Kind::ReturnToLoad) {
      const bool returningToSave = routeMapReturnSave.value_or(false);
      const int selection = routeMapReturnSelection;
      openSaveLoad(returningToSave, false);
      systemUi.RestoreSaveLoadSelection(selection);
      routeMapReturnSave.reset();
    } else if (output.routeMapAction.kind == Kind::NodeRequested) {
      // SAVE opens RouteMap as a viewer. Selecting a checkpoint must not
      // replace the narrative owner (or a pending SaveFeel handoff).
      if (routeMapReturnSave.value_or(false))
        return;
      try {
        // A globally unlocked SP does not guarantee that the current SLog
        // contains the native tag3 checkpoint needed to resume it. Keep the
        // Route Map usable if this session lacks that snapshot.
        loadCheckpoint(output.routeMapAction.selector);
        routeMapReturnSave.reset();
        closePanels();
      } catch (const std::exception &error) {
        setStatus(error.what());
      }
    }
  }
  if (output.hasSaveLoadOutput) {
    using Action = Kotonoha::SchoolDaysSaveLoadModel::Action;
    const auto &result = output.saveLoadOutput;
    switch (result.action) {
    case Action::Close:
      if (bridge.HasPendingHandoff() &&
          handoffUiState == HandoffUiState::SaveLoad) {
        handoffUiState = HandoffUiState::Continuing;
        closePanels();
        continueHandoff();
      } else if (frontendChild == FrontendChild::Load) {
        beginChildToTitle(SDL_GetTicks());
      } else if (!beginSystemPanelClose()) {
        closePanels();
      }
      break;
    case Action::RouteMap:
      // The retail nested Replay Data LOAD reader is authenticated, but a
      // RouteMap handoff from that private owner is not. Consume it in place.
      if (saveLoadOwner != SaveLoadOwner::PlayData)
        openRouteMap(false);
      break;
    case Action::LoadSlotRequested:
      try {
        if (saveLoadOwner == SaveLoadOwner::PlayData) {
          // A private Replay Data LOAD does not take ownership of the main
          // narrative presentation or its retail loading transition.
          loadSlot(result.slot);
        } else if (saveLoadOwner == SaveLoadOwner::Narrative &&
                   !loadTransition.Begin(result.slot, SDL_GetTicks())) {
          setStatus("School Days LOAD is already in progress");
        } else if (saveLoadOwner == SaveLoadOwner::None) {
          // Preserve the old direct path for callers without a native
          // SaveLoad owner (test/debug and engine fallback paths).
          loadSlot(result.slot);
        }
      } catch (const std::exception &error) {
        setStatus(error.what());
      }
      break;
    case Action::SaveSlotRequested:
      try {
        saveSlot(result.slot, result.comment);
        setStatus("Saved to slot " + std::to_string(result.slot + 1));
      } catch (const std::exception &error) {
        SDL_LogError(Kotonoha::LogUI, "School Days save slot %d: %s",
                     result.slot, error.what());
        setStatus(error.what());
      }
      saveLoadPage = -1;
      break;
    default:
      break;
    }
  }
}

void Runtime::playSound(Ui::UiSfxCue cue) {
  if (!engine || !hasProfile || cue == Ui::UiSfxCue::None)
    return;
  const std::string *stem = nullptr;
  switch (cue) {
  case Ui::UiSfxCue::Cancel:
    stem = &profile.seCancel;
    break;
  case Ui::UiSfxCue::Select:
    stem = &profile.seSelect;
    break;
  case Ui::UiSfxCue::Click:
    stem = &profile.seClick;
    break;
  case Ui::UiSfxCue::Up:
    stem = &profile.seUp;
    break;
  case Ui::UiSfxCue::Down:
    stem = &profile.seDown;
    break;
  case Ui::UiSfxCue::View:
    stem = &profile.seView;
    break;
  case Ui::UiSfxCue::Open:
    stem = &profile.seOpen;
    break;
  default:
    return;
  }
  if (stem->empty())
    return;
  traceDev((std::string("Sfx:") + *stem).c_str());
  try {
    auto sound = std::make_unique<Kotonoha::MediaPlayer>(engine->GetAudio());
    sound->setBufferSeconds(SchoolDaysMediaCacheSeconds);
    sound->open(*assets, assets->resolve(*stem + ".ogg"), true, "SE");
    sounds.push_back(std::move(sound));
  } catch (const std::exception &error) {
    SDL_LogWarn(Kotonoha::LogUI, "School Days system sound %s: %s",
                stem->c_str(), error.what());
  }
}

void Runtime::loadCheckpoint(int selector) {
  Kotonoha_KtrfError error{};
  // Validate the tag3 SLog checkpoint before retiring any pointers to the
  // currently presented scene. GlobalFlag SP flags alone are not resumable.
  Kotonoha::SchoolDaysKtrfSession::RouteMapCheckpointPlan plan;
  Check(bridge.Session().PlanRouteMapCheckpoint(selector, &plan, &error),
        error);
  retireSceneEventPointers();
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.LoadRouteMapSelector(selector, &result, &error), error);
  frontendChild = FrontendChild::None;
  backlogStore.ResetTransient();
  if (backlogFallback)
    backlogFallback->clear();
  backlogIndex = 0;
  backlogScene.clear();
  openScene(result.scene);
}

void Runtime::flushGlobal() {
  if (documents.empty() || !bridge.Session().Persistence().HasGlobalFlag())
    return;
  std::vector<uint8_t> bytes;
  Kotonoha_KtrfError error{};
  Check(bridge.Session().SerializeGlobalFlag(&bytes, &error), error);
  std::string failure;
  if (storage.IsInitialized())
    Check(storage.WriteGlobalFlagBytes(bytes, &failure), failure,
          "School Days GlobalFlag write failed");
  else
    WriteBytes((documents / "GlobalFlag").string(), bytes);
}

void Runtime::saveGame(const std::string &path) {
  if (temporaryPlaybackActive())
    throw std::runtime_error(
        "School Days save is unavailable during Replay playback");
  Kotonoha_KtrfError error{};
  auto &session = bridge.Session();
  if (!bridge.HasPendingHandoff())
    Check(session.Persistence().RefreshCurrentScriptSnapshot(session.State(),
                                                             &error),
          error);
  std::vector<uint8_t> bytes;
  Check(session.Persistence().SerializeSaveLogWithLiveChoiceCommitMarker(
            !bridge.HasPendingHandoff() && committed, &bytes, &error),
        error);
  WriteBytes(path, bytes);
  flushGlobal();
}

void Runtime::loadGame(const std::string &path) {
  if (temporaryPlaybackActive())
    throw std::runtime_error(
        "School Days load is unavailable during Replay playback");
  prepareCarryFrame();
  playing.clear();
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  const auto saveBytes = assets->read(path);
  retireSceneEventPointers();
  Check(bridge.LoadSave(saveBytes, &result, &error), error);
  closePanels();
  backlogStore.ResetTransient();
  if (backlogFallback)
    backlogFallback->clear();
  backlogIndex = 0;
  backlogScene.clear();
  openScene(result.scene);
}

std::string Runtime::debugNode() const {
  return temporaryPlaybackActive() ? scene.sceneKey
                                   : bridge.CurrentScene().sceneKey;
}

std::string Runtime::nodeClockKey() const {
  const std::string sceneKey = debugNode();
  // Share the node clock between positions of the same original scene;
  // position-specific media/events still use their own event clock.
  const auto dash = sceneKey.rfind('-');
  return dash == std::string::npos ? sceneKey : sceneKey.substr(0, dash);
}

std::vector<Kotonoha::DebugNode> Runtime::debugNodes() const {
  const auto *document = bridge.Session().Adapter()->document;
  std::vector<Kotonoha::DebugNode> nodes;
  const auto *section = Kotonoha_KtrfFindSection(document, "NODE");
  Kotonoha_KtrfError error{};
  if (!section)
    return nodes;
  for (uint32_t i = 0; i < section->item_count; ++i) {
    Kotonoha_KtrfNode node{};
    Check(Kotonoha_KtrfGetNode(document, i, &node, &error), error);
    const char *label = nullptr;
    size_t size = 0;
    Check(Kotonoha_KtrfGetString(document,
                                 node.label_str == KOTONOHA_KTRF_NULL_INDEX
                                     ? node.id_str
                                     : node.label_str,
                                 &label, &size, &error),
          error);
    Kotonoha::DebugNode output{std::to_string(i), std::string(label, size), {}};
    Kotonoha::SchoolDaysKtrfScene resolved;
    if (bridge.Session().ResolveSceneNode(i, &resolved, &error))
      output.assets.push_back(resolved.orsPath);
    nodes.push_back(std::move(output));
  }
  return nodes;
}

std::vector<Kotonoha::DebugAudioAsset> Runtime::debugSceneAudio() {
  std::vector<Kotonoha::DebugAudioAsset> result;
  for (auto *voice = script.data; voice; voice = voice->next) {
    if (voice->command != PLAY_VOICE || !voice->data.play_voice ||
        !voice->data.play_voice->path)
      continue;
    Kotonoha::DebugAudioAsset audio{
        std::string(Text(voice->data.play_voice->path)) + ".OGG",
        Text(voice->data.play_voice->character_short),
        {}};
    // ORS voice and subtitle events normally overlap. Prefer the full subtitle
    // speaker name and retain its text for the dubbing booth.
    for (auto *line = script.data; line; line = line->next) {
      if (line->command != PRINT_TEXT || !line->data.print_text ||
          line->startTick >= voice->endTick ||
          line->endTick <= voice->startTick)
        continue;
      audio.character = Text(line->data.print_text->character);
      audio.text = Text(line->data.print_text->text);
      break;
    }
    auto duplicate =
        std::find_if(result.begin(), result.end(),
                     [&](const auto &item) { return item.id == audio.id; });
    if (duplicate == result.end())
      result.push_back(std::move(audio));
    else if (duplicate->character.empty() && !audio.character.empty())
      *duplicate = std::move(audio);
  }
  return result;
}

void Runtime::debugOverlayAssetChanged(const std::string &) {
  // Voices are prepared ahead of their authored start. Discard prepared
  // originals so the next playback resolves the freshly written overlay.
  resetPreparedMedia();
}

std::vector<std::string> Runtime::activeTranslationKeys() const {
  std::vector<std::string> keys;
  if (subtitles) {
    auto texts = subtitles->currentOriginalCueTexts();
    keys.reserve(texts.size());
    for (auto &text : texts)
      keys.push_back(std::move(text));
  }
  return keys;
}

Kotonoha::Translation::SourceTexts Runtime::translationSources() const {
  Kotonoha::Translation::SourceTexts sources;
  if (subtitles) {
    auto texts = subtitles->originalCueTexts();
    if (!texts.empty())
      sources[0] = std::move(texts);
  }
  return sources;
}

void Runtime::debugJumpToNode(const std::string &id) {
  if (temporaryPlaybackActive())
    throw std::runtime_error(
        "School Days debug jump is unavailable during Replay playback");
  Kotonoha_KtrfError error{};
  uint32_t index = 0;
  if (!id.empty() && id.find_first_not_of("0123456789") == std::string::npos) {
    const auto parsed = std::stoull(id);
    if (parsed > UINT32_MAX)
      throw std::out_of_range("School Days node index");
    index = uint32_t(parsed);
  } else {
    Kotonoha::SchoolDaysKtrfScene resolved;
    Check(bridge.Session().ResolveSceneKey(id, &resolved, &error), error);
    index = resolved.nodeIndex;
  }
  retireSceneEventPointers();
  Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
  Check(bridge.DebugJumpToNode(index, &result, &error), error);
  openScene(result.scene);
}

void Runtime::debugRestartScene() {
  if (temporaryPlaybackActive())
    throw std::runtime_error(
        "School Days scene restart is unavailable during Replay playback");
  debugJumpToNode(debugNode());
}

bool Runtime::advanceStoryTime(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0)
    throw std::invalid_argument("School Days scene step must be positive");
  const auto amount = static_cast<Kotonoha_SceneTick>(
      std::max(1.0, std::round(seconds * 24.0)));
  const auto maximum = std::numeric_limits<Kotonoha_SceneTick>::max();
  seek(tick > maximum - amount ? maximum : tick + amount);
  return true;
}

void Runtime::loadKtrf(const std::string &path) {
  if (temporaryPlaybackActive())
    throw std::runtime_error(
        "School Days route replacement is unavailable during Replay playback");
  // Validate and probe first so a malformed document cannot destroy the live
  // route.
  Kotonoha::SchoolDaysKtrfSession candidate;
  Kotonoha_KtrfError error{};
  Check(
      candidate.Open(path.c_str(), assets->orsRoot().string().c_str(), &error),
      error);
  candidate.Close();
  std::vector<uint8_t> global;
  const bool hasGlobal = bridge.Session().Persistence().HasGlobalFlag();
  if (hasGlobal)
    Check(bridge.Session().SerializeGlobalFlag(&global, &error), error);
  flushGlobal();
  retireSceneEventPointers();
  Check(bridge.Open(path.c_str(), assets->orsRoot().string().c_str(), nullptr,
                    {createPresentation, destroyPresentation, this}, &error),
        error);
  if (hasGlobal)
    Check(bridge.Session().LoadGlobalFlag(global, &error), error);
  routePath = path;
  openScene(bridge.CurrentScene());
}

void Runtime::dumpKtrf(const std::string &path) const {
  const auto *document = bridge.Session().Adapter()->document;
  if (!document)
    throw std::runtime_error("No School Days KTRF loaded");
  Kotonoha::writeTextFileAtomically(
      path, std::string(reinterpret_cast<const char *>(document->bytes),
                        document->size));
}

} // namespace SchoolDays
