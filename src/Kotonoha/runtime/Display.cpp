#include <Kotonoha/Display.hpp>

#include <algorithm>
#include <cmath>

namespace Kotonoha {

bool GetDisplayMode(const Kotonoha_Game &context, DisplayMode *mode) {
  if (context.window == nullptr || mode == nullptr) {
    SDL_SetError("Display mode query requires a window and output value");
    return false;
  }
  const SDL_WindowFlags flags = SDL_GetWindowFlags(context.window);
  if ((flags & SDL_WINDOW_FULLSCREEN) == 0) {
    *mode = DisplayMode::Windowed;
  } else {
    *mode = SDL_GetWindowFullscreenMode(context.window) != nullptr
                ? DisplayMode::ExclusiveFullscreen
                : DisplayMode::BorderlessFullscreen;
  }
  return true;
}

SDL_DisplayID ResolveDisplayIndex(int index) {
  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  if (displays == nullptr || count <= 0) return 0;
  if (index < 0 || index >= count) index = 0;
  const SDL_DisplayID result = displays[index];
  SDL_free(displays);
  return result;
}

int DisplayIndexForId(SDL_DisplayID display) {
  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  if (displays == nullptr || count <= 0) return 0;
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (displays[i] == display) {
      index = i;
      break;
    }
  }
  SDL_free(displays);
  return index;
}

std::vector<DisplayResolution>
EnumerateDisplayResolutions(SDL_DisplayID display) {
  std::vector<DisplayResolution> out;
  if (display == 0) display = ResolveDisplayIndex(0);
  if (display == 0) return out;

  int count = 0;
  SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
  if (modes != nullptr) {
    for (int i = 0; i < count; ++i) {
      const SDL_DisplayMode *m = modes[i];
      if (m == nullptr || m->w <= 0 || m->h <= 0) continue;
      DisplayResolution candidate{m->w, m->h, m->refresh_rate,
                                  m->pixel_density > 0.0f ? m->pixel_density
                                                          : 1.0f};
      const bool duplicate = std::any_of(
          out.begin(), out.end(), [&](const DisplayResolution &v) {
            return v.width == candidate.width && v.height == candidate.height &&
                   std::fabs(v.refreshRate - candidate.refreshRate) < 0.01f;
          });
      if (!duplicate) out.push_back(candidate);
    }
    SDL_free(modes);
  }

  const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
  if (desktop != nullptr && desktop->w > 0 && desktop->h > 0) {
    DisplayResolution candidate{desktop->w, desktop->h, desktop->refresh_rate,
                                desktop->pixel_density > 0.0f
                                    ? desktop->pixel_density
                                    : 1.0f};
    const bool duplicate = std::any_of(
        out.begin(), out.end(), [&](const DisplayResolution &v) {
          return v.width == candidate.width && v.height == candidate.height &&
                 std::fabs(v.refreshRate - candidate.refreshRate) < 0.01f;
        });
    if (!duplicate) out.push_back(candidate);
  }

  std::sort(out.begin(), out.end(), [](const DisplayResolution &a,
                                       const DisplayResolution &b) {
    const long long areaA = static_cast<long long>(a.width) * a.height;
    const long long areaB = static_cast<long long>(b.width) * b.height;
    if (areaA != areaB) return areaA < areaB;
    if (a.width != b.width) return a.width < b.width;
    if (a.height != b.height) return a.height < b.height;
    return a.refreshRate < b.refreshRate;
  });
  return out;
}

bool SetDisplay(Kotonoha_Game &context, DisplayMode mode,
                SDL_DisplayID targetDisplay, int width, int height,
                float refreshRate) {
  if (mode != DisplayMode::Windowed &&
      mode != DisplayMode::ExclusiveFullscreen &&
      mode != DisplayMode::BorderlessFullscreen) {
    SDL_SetError("Invalid Kotonoha display mode");
    return false;
  }
  if (context.window == nullptr) {
    SDL_SetError("Display mode change requires a window");
    return false;
  }

  SDL_Window *window = context.window;
  const SDL_DisplayID currentDisplay = SDL_GetDisplayForWindow(window);
  if (targetDisplay == 0) targetDisplay = currentDisplay;
  if (targetDisplay == 0) targetDisplay = ResolveDisplayIndex(0);
  if (targetDisplay == 0) {
    SDL_SetError("No display is available for the window");
    return false;
  }

  SDL_Rect targetBounds{};
  if (!SDL_GetDisplayBounds(targetDisplay, &targetBounds)) return false;

  DisplayMode currentMode = DisplayMode::Windowed;
  if (!GetDisplayMode(context, &currentMode)) return false;
  auto sync = [&]() {
    context.flags = SDL_GetWindowFlags(window);
    DisplayMode actual = DisplayMode::Windowed;
    if (GetDisplayMode(context, &actual)) {
      context.config.fullScreen = actual != DisplayMode::Windowed;
      context.config.displayMode = static_cast<int>(actual);
      context.config.displayIndex = DisplayIndexForId(
          SDL_GetDisplayForWindow(window));
    }
  };
  auto fail = [&](const char *operation) {
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Display change failed to %s: %s",
                 operation, SDL_GetError());
    sync();
    return false;
  };

  if (currentMode != DisplayMode::Windowed) {
    if (!SDL_SetWindowFullscreen(window, false)) return fail("leave fullscreen");
    if (!SDL_SyncWindow(window)) return fail("sync after leaving fullscreen");
  }
  if (targetDisplay != currentDisplay) {
    if (!SDL_SetWindowPosition(window, targetBounds.x + 50,
                               targetBounds.y + 50))
      return fail("move to target display");
    if (!SDL_SyncWindow(window)) return fail("sync on target display");
  }

  switch (mode) {
  case DisplayMode::Windowed:
    if (!SDL_SetWindowFullscreenMode(window, nullptr))
      return fail("clear fullscreen mode");
    // Some Windows compositors leave a borderless-fullscreen window in the
    // maximized state after SDL exits fullscreen.  Restore it before applying
    // the persisted window size so WINDOW reliably means an ordinary window,
    // not a maximized desktop-sized surface.
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
      if (!SDL_RestoreWindow(window)) return fail("restore maximized window");
      if (!SDL_SyncWindow(window)) return fail("sync restored window");
    }
    if (!SDL_SetWindowBordered(window, true))
      return fail("restore window border");
    if (!SDL_SetWindowSize(window, context.config.windowSize[0],
                           context.config.windowSize[1]))
      return fail("set window size");
    break;
  case DisplayMode::ExclusiveFullscreen: {
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(targetDisplay);
    if (desktop == nullptr) return fail("query desktop mode");
    if (width <= 0) width = desktop->w;
    if (height <= 0) height = desktop->h;
    if (refreshRate <= 0.0f) refreshRate = desktop->refresh_rate;
    SDL_DisplayMode requested{};
    SDL_ClearError();
    if (!SDL_GetClosestFullscreenDisplayMode(targetDisplay, width, height,
                                             refreshRate, true, &requested))
      return fail("resolve exclusive mode");
    if (!SDL_SetWindowFullscreenMode(window, &requested))
      return fail("set exclusive mode");
    if (!SDL_SetWindowFullscreen(window, true))
      return fail("enter exclusive fullscreen");
    context.config.fullscreenSize[0] = requested.w;
    context.config.fullscreenSize[1] = requested.h;
    context.config.fullscreenRefreshMilliHz =
        static_cast<int>(requested.refresh_rate * 1000.0f + 0.5f);
    break;
  }
  case DisplayMode::BorderlessFullscreen:
    if (!SDL_SetWindowFullscreenMode(window, nullptr))
      return fail("select borderless mode");
    if (!SDL_SetWindowFullscreen(window, true))
      return fail("enter borderless fullscreen");
    break;
  }

  if (!SDL_SyncWindow(window)) return fail("sync requested mode");
  sync();
  DisplayMode actual = DisplayMode::Windowed;
  if (!GetDisplayMode(context, &actual) || actual != mode) {
    SDL_SetError("Window manager rejected the requested display mode");
    return fail("verify requested mode");
  }
  context.config.dirtyConfig = true;
  return true;
}

} // namespace Kotonoha
