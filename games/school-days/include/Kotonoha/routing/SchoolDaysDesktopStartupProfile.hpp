#pragma once

#include <Kotonoha/core/Settings.hpp>
#include <cmath>

namespace Kotonoha {

// Physical Windows display selection belongs to the School Days host, not
// Config.DAT's authored 4:3/16:9 layout. Never overwrite a stored choice.
class SchoolDaysDesktopStartupProfile final {
public:
    static bool Apply(EngineSettings &settings, bool hasStoredSettings,
                      int desktopWidth, int desktopHeight, float refreshRate) {
        if (desktopWidth < 320 || desktopHeight < 240 ||
            desktopWidth > 16384 || desktopHeight > 16384 ||
            refreshRate <= 0.0f || refreshRate > 1000.0f)
            return false;

        // First launch: use the monitor's current desktop mode, regardless
        // of whatever viewport-size default is compiled into the engine.
        if (!hasStoredSettings) {
            settings.width = desktopWidth;
            settings.height = desktopHeight;
            settings.fullscreenWidth = desktopWidth;
            settings.fullscreenHeight = desktopHeight;
            settings.refreshRate = refreshRate;
            return true;
        }

        // Back-compat: only migrate the physical tuple from very old files.
        // The saved mode and window size MUST remain as chosen by the player.
        if (settings.fullscreenWidth <= 0 ||
            settings.fullscreenHeight <= 0 || settings.refreshRate <= 0.0f) {
            settings.fullscreenWidth = desktopWidth;
            settings.fullscreenHeight = desktopHeight;
            settings.refreshRate = refreshRate;
            return true;
        }
        return false;
    }

    // A borderless window always displays at the physical desktop mode.
    // Choosing a different resolution is an explicit user request: switch
    // to exclusive mode rather than silently storing a value that won't be
    // applied. Windowed choices control the host's window dimensions.
    static bool SelectResolution(EngineSettings &settings,
                                 int selectedWidth, int selectedHeight,
                                 float selectedHz,
                                 int desktopWidth, int desktopHeight,
                                 float desktopHz = 0.0f) {
        if (selectedWidth < 320 || selectedHeight < 240 ||
            selectedWidth > 16384 || selectedHeight > 16384 ||
            selectedHz <= 0.0f || selectedHz > 1000.0f)
            return false;
        if (settings.mode == WindowMode::Windowed) {
            settings.width = selectedWidth;
            settings.height = selectedHeight;
        } else if (settings.mode == WindowMode::Borderless &&
                   (selectedWidth != desktopWidth ||
                    selectedHeight != desktopHeight ||
                    (desktopHz > 0.0f &&
                     std::abs(selectedHz - desktopHz) > 0.75f))) {
            settings.mode = WindowMode::Exclusive;
        }
        settings.fullscreenWidth = selectedWidth;
        settings.fullscreenHeight = selectedHeight;
        settings.refreshRate = selectedHz;
        return true;
    }

    // General WINDOW = decorated desktop-resolution window; FULL = real
    // exclusive fullscreen. Do not overwrite the manually chosen exclusive
    // resolution/refresh from the launcher or Display Options simply because
    // the player switches from WINDOW to FULL (or back).  Only absent or
    // malformed fullscreen dimensions should fall back to the desktop mode.
    static bool SelectGeneralWindowSize(EngineSettings &settings, bool full,
                                        int desktopWidth, int desktopHeight,
                                        float desktopHz = 0.0f) {
        const WindowMode desired = full ? WindowMode::Exclusive : WindowMode::Windowed;
        bool changed = settings.mode != desired;
        if (!full && !settings.resizable) {
            settings.resizable = true; // Native maximize and size grip must work.
            changed = true;
        }
        if (desktopWidth >= 320 && desktopHeight >= 240) {
            if (settings.width != desktopWidth || settings.height != desktopHeight)
                changed = true;
            settings.width = desktopWidth;
            settings.height = desktopHeight;
            if (settings.fullscreenWidth < 320 || settings.fullscreenHeight < 240 ||
                settings.fullscreenWidth > 16384 || settings.fullscreenHeight > 16384) {
                // A first-run profile without an exclusive tuple can still
                // enter FULL using the monitor's authenticated desktop mode.
                settings.fullscreenWidth = desktopWidth;
                settings.fullscreenHeight = desktopHeight;
                changed = true;
            }
        }
        if (desktopHz > 0.0f && (settings.refreshRate <= 0.0f ||
            settings.refreshRate > 1000.0f)) {
            settings.refreshRate = desktopHz;
            changed = true;
        }
        settings.mode = desired;
        return changed;
    }
};

} // namespace Kotonoha
