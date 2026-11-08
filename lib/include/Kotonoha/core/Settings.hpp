#pragma once

#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/media/upscaling/Upscaler.hpp>
#include <map>
#include <string>

namespace Kotonoha {

struct EngineSettings {
  int width = 1280, height = 720;
  int display = 0;
  WindowMode mode = defaultWindowMode();
  int fullscreenWidth = 0, fullscreenHeight = 0;
  float refreshRate = 0;
  bool resizable = true;
  bool vsync = true;
  // Swap the controller face buttons used for confirm/cancel. Shown in the
  // settings UI only while at least one gamepad is connected.
  bool swapControllerAB = false;
  float masterVolume = 1;
  bool masterMuted = false;
  std::map<std::string, float> channelVolumes;
  std::string vnFontPath;
  bool vnTextColorOverride = false;
  SDL_Color vnTextColor{255, 255, 255, 255};
  SDL_Color vnSpeakerColor{255, 215, 100, 255};
  float debugUIScale = 1;
  // Fraction of the fitted physical Novel viewport used for the intermediate
  // render target. Screen-space UI is always rendered at native output size.
  float renderScale = 1;
  std::string renderer;
  UpscaleSettings upscaling;
  SDL_LogPriority logPriority = SDL_LOG_PRIORITY_INFO;
  std::string translationPath;
  // Asset overlays kept by name: friendly name -> absolute path. The absolute
  // path is always tried first; if it no longer exists, the name is resolved
  // through the standard overlay folder search.
  std::map<std::string, std::string> assetOverlays;
  std::string assetOverlayName;
  // Absolute path of the overlay currently in effect (derived from
  // assetOverlays[assetOverlayName] or from a direct command-line/path choice).
  // Not persisted on its own; it is recomputed from the persisted map.
  std::string assetOverlayPath;
  std::string fontPath = "NotoSansJP-Regular.ttf";
  bool enableForcedSubtitles = true;  // Toggle for forced subtitles system
  void validate() const;
  static EngineSettings load(const std::string &path);
  static EngineSettings load(const std::string &path,
                             const EngineSettings &defaults);
  // <pref path>/Kotonoha/<identify>/settings.json, as used by the Engine.
  static std::string defaultPath(const std::string &identify = {});
  void save(const std::string &path) const;
  // Absolute path currently in effect: persisted absolute for the selected
  // name if it still exists, otherwise a name-based overlay search.
  std::string resolvedOverlayPath() const;
};

} // namespace Kotonoha
