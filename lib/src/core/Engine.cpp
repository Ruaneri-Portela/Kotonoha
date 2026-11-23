#include <Kotonoha/Filesystem/StoragePaths.hpp>
#include <Kotonoha/components/ForcedSubtitleRenderer.hpp>
#include <Kotonoha/components/InGameMenu.hpp>
#include <Kotonoha/components/SettingsMenu.hpp>
#include <Kotonoha/components/VNBacklog.hpp>
#include <Kotonoha/components/VNManager.hpp>
#include <Kotonoha/components/VNTextBox.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/events/BackAction.hpp>
#include <Kotonoha/mcp/MCPServer.hpp>
#include <Kotonoha/mobile/AndroidDisplay.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace Kotonoha {
namespace {
// When HTML5 media is unexpectedly paused by the browser or its controls,
// freeze the story clock as well. The browser bridge excludes intentional
// pauses, loading, completed playback and failed media sessions.
bool browserMediaPlaybackBlocked() {
#ifdef __EMSCRIPTEN__
  return EM_ASM_INT({
           const callback = globalThis["KotonohaMediaPlaybackBlocked"];
           return typeof callback == "function" && callback() ? 1 : 0;
         }) != 0;
#else
  return false;
#endif
}
} // namespace

std::vector<DebugNode> Engine::debugNodes() {
  auto custom = runtime.debugNodes();
  if (!custom.empty())
    return custom;
  auto &manager = GetVNManagerRuntime(*this);
  std::vector<DebugNode> result;
  for (int id : manager.getNodeIds()) {
    const auto &node = manager.getScriptNode(id);
    DebugNode item{std::to_string(id), node.name, {}};
    for (const auto &asset : {node.backgroundAsset, node.characterAsset})
      if (!asset.empty())
        item.assets.push_back(asset);
    for (const auto &cue : node.timeline.cues)
      if ((cue.type == CueType::Video || cue.type == CueType::Audio) &&
          !cue.value.empty())
        item.assets.push_back(cue.value);
    result.push_back(std::move(item));
  }
  return result;
}

void Engine::debugJumpToNode(const std::string &id) {
  if (settingsMenu->isOpen())
    throw std::logic_error("Close engine settings before jumping to a node");
  if (!runtime.debugNodes().empty()) {
    runtime.debugJumpToNode(id);
  } else {
    int number = -1;
    const auto parsed =
        std::from_chars(id.data(), id.data() + id.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size())
      throw std::invalid_argument("Invalid script node ID");
    GetVNManagerRuntime(*this).debugJumpToNode(number);
  }
  resetNodeClock();
}

void Engine::debugRestartScene() {
  if (settingsMenu->isOpen())
    throw std::logic_error("Close engine settings before restarting the scene");
  if (!runtime.debugNode().empty()) {
    runtime.debugRestartScene();
  } else {
    auto &manager = GetVNManagerRuntime(*this);
    const int current = manager.getCurrentNodeId();
    if (current < 0 || manager.isFinished())
      throw std::logic_error("There is no active scene to restart");
    manager.debugJumpToNode(current);
  }
  resetNodeClock();
}

void Engine::debugAdvanceScene(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0 || seconds > 60)
    throw std::invalid_argument("Scene step must be within 0..60 seconds");
  if (runtime.advanceStoryTime(seconds)) {
    frameClock.reset();
    return;
  }
  if (!timeline->isPlaying())
    throw std::logic_error("The active scene has no time-based timeline");
  timeline->setPaused(false);
  timeline->update(seconds);
  runtime.update();
  timeline->setPaused(isStoryPaused());
  frameClock.reset();
}

void Engine::debugAdvanceEvent() {
  if (!runtime.handleAdvanceAction()) {
    auto &manager = GetVNManagerRuntime(*this);
    if (manager.getCurrentNodeId() >= 0 && !manager.isFinished())
      manager.advance();
    else if (!runtime.advanceStoryTime(1.0 / 24.0))
      throw std::logic_error("The active runtime cannot advance an event");
  }
  frameClock.reset();
}

void Engine::loadKtrf(const std::string &path) {
  if (settingsMenu->isOpen())
    throw std::logic_error("Close engine settings before loading KTRF");
  if (runtime.hasCustomKtrf()) {
    runtime.loadKtrf(path);
  } else {
    if (!runtime.debugNodes().empty())
      throw std::logic_error(
          "This game's native scripts do not use the VN KTRF profile");
    if (runtime.GetState() != GameState::Gameplay)
      throw std::logic_error("Start gameplay before loading KTRF");
    GetVNManagerRuntime(*this).loadKtrf(path);
  }
  SDL_LogInfo(LogCore, "Loaded KTRF: %s", path.c_str());
}

void Engine::dumpKtrf(const std::string &path) {
  if (runtime.hasCustomKtrf())
    runtime.dumpKtrf(path);
  else {
    if (!runtime.debugNodes().empty())
      throw std::logic_error(
          "This game's native scripts do not use the VN KTRF profile");
    GetVNManagerRuntime(*this).dumpKtrf(path);
  }
  SDL_LogInfo(LogCore, "Dumped KTRF: %s", path.c_str());
}

std::string EngineOptions::assetOverlayDirectory() const {
  if (assetOverlaySpecified)
    return assetOverlayPath;
  if (!settings.assetOverlayName.empty()) {
    const auto &overlays = settings.assetOverlays;
    auto i = overlays.find(settings.assetOverlayName);
    std::error_code dirEc;
    if (i != overlays.end() && !i->second.empty() &&
        std::filesystem::is_directory(i->second, dirEc) && !dirEc)
      return i->second;
    // Persisted absolute path no longer exists: resolve by name through the
    // standard overlay folder search.
    auto found = findAssetOverlayDirectory(settings.assetOverlayName);
    if (!found.empty())
      return found.string();
    return {};
  }
  if (!settings.assetOverlayPath.empty())
    return settings.assetOverlayPath;
  return findAssetOverlayDirectory().string();
}
SDLSession::SDLSession(const Runtime &runtime) {
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  if (!SDL_SetAppMetadata(runtime.name.c_str(), runtime.version.c_str(),
                          runtime.identify.c_str()) ||
      !SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                SDL_INIT_GAMEPAD))
    throw std::runtime_error(SDL_GetError());
  int count = 0;
  SDLMemoryPtr<SDL_JoystickID> ids{SDL_GetGamepads(&count)};
  for (int i = 0; ids && i < count; ++i)
    openGamepad(ids.get()[i]);
}
void SDLSession::openGamepad(SDL_JoystickID id) {
  if (gamepads.count(id))
    return;
  GamepadPtr gamepad{SDL_OpenGamepad(id)};
  if (gamepad)
    gamepads.emplace(id, std::move(gamepad));
  else
    SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cannot open gamepad: %s",
                SDL_GetError());
}
void SDLSession::deviceEvent(EventRef event) {
  if (event.type == SDL_EVENT_GAMEPAD_ADDED)
    openGamepad(event.gdevice.which);
  else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
    gamepads.erase(event.gdevice.which);
  else if (event.type == SDL_EVENT_JOYSTICK_ADDED &&
           SDL_IsGamepad(event.jdevice.which))
    openGamepad(event.jdevice.which);
  else if (event.type == SDL_EVENT_JOYSTICK_REMOVED)
    gamepads.erase(event.jdevice.which);
}
SDLSession::~SDLSession() {
  gamepads.clear();
  // Do not call SDL_Quit() here: SDL3 callback apps tear down SDL after
  // SDL_AppQuit(), and calling SDL_Quit() when returning from a game to
  // KotonohaLauncher inside SDL_AppIterate destroys active UIKit/Cocoa state.
}

Engine::Engine(Runtime &run, EngineOptions options)
    : runtime(run), log(options.logPath), session(runtime),
      assets(options.assets ? std::move(options.assets)
                            : std::make_shared<FileSystemAssetProvider>()),
      settings(options.settings), settingsPath(options.settingsPath) {
  if (!options.subtitleStylePath.empty())
    subtitleStyle = AssStyle::load(options.subtitleStylePath);
  if (settingsPath.empty() && options.persistSettings)
    settingsPath = EngineSettings::defaultPath(runtime.identify);
  std::error_code existsEc;
  const bool storedSettings = !settingsPath.empty() &&
                              std::filesystem::exists(settingsPath, existsEc) &&
                              !existsEc;
  settingsLoadedFromDisk = storedSettings;
  if (storedSettings)
    settings = EngineSettings::load(settingsPath, settings);
  // A fresh window follows the game viewport aspect (default height kept).
  const SDL_Point view = runtime.viewport();
  if (view.x <= 0 || view.y <= 0)
    throw std::invalid_argument("Runtime viewport must be positive");
  const EngineSettings defaults;
  if (!storedSettings && settings.width == defaults.width &&
      settings.height == defaults.height)
    settings.width = static_cast<int>(
        std::lround(static_cast<double>(settings.height) * view.x / view.y));
  if (!options.rendererOverride.empty())
    settings.renderer = options.rendererOverride;
#if KOTONOHA_ENABLE_UPSCALER
  if (options.upscalerBackendOverride.has_value()) {
    settings.upscaling.backend = *options.upscalerBackendOverride;
    if (settings.upscaling.backend == UpscaleBackend::None &&
        !options.upscalerFactorOverride.has_value())
      settings.upscaling.factor = 1.0f;
  }
  if (options.upscalerPresetOverride.has_value()) {
    settings.upscaling.preset = *options.upscalerPresetOverride;
    settings.upscaling.shaderFiles.clear();
  }
  if (options.upscalerFactorOverride.has_value())
    settings.upscaling.factor = *options.upscalerFactorOverride;
#endif
  settings.validate();
#if !KOTONOHA_ENABLE_UPSCALER
  // A build without this module never inherits a saved backend/preset.
  settings.upscaling = {};
  settings.upscaling.backend = UpscaleBackend::None;
#endif
  if (options.translationPath.empty()) {
    if (!settings.translationPath.empty())
      options.translationPath = settings.translationPath;
    else if (const char *path = SDL_getenv("KOTONOHA_TRANSLATION"))
      options.translationPath = path;
  }
  if (!options.translationPath.empty())
    translation.load(options.translationPath);
  SDL_WindowFlags graphicsFlags = 0;
#if KOTONOHA_ENABLE_UPSCALER
  if (isMobilePlatform() && settings.upscaling.backend == UpscaleBackend::MPV &&
      options.rendererOverride.empty())
    settings.renderer = "opengles2";
#endif
  if (settings.renderer == "opengles2") {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    graphicsFlags = SDL_WINDOW_OPENGL;
  }
  window.reset(
      SDL_CreateWindow(runtime.name.c_str(), settings.width, settings.height,
                       (settings.resizable ? SDL_WINDOW_RESIZABLE : 0) |
                           SDL_WINDOW_HIGH_PIXEL_DENSITY | graphicsFlags));
  if (!window)
    throw std::runtime_error(SDL_GetError());
  renderer.reset(SDL_CreateRenderer(
      window.get(),
      settings.renderer.empty() ? nullptr : settings.renderer.c_str()));
  if (!renderer ||
      !SDL_SetRenderLogicalPresentation(renderer.get(), view.x, view.y,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX))
    throw std::runtime_error(SDL_GetError());
  upscaler = std::make_unique<Upscaling>(*renderer);
  {
    int displayCount = 0;
    SDLArrayPtr<SDL_DisplayID> displays(SDL_GetDisplays(&displayCount));
    if (displays && displayCount > 0 &&
        (settings.display < 0 || settings.display >= displayCount)) {
      SDL_LogWarn(LogCore,
                  "Configured display %d is unavailable (%d available); "
                  "falling back to display 1",
                  settings.display + 1, displayCount);
      settings.display = 0;
    }
  }
  applyWindowSettings(settings);
  if (!upscaler->supports(settings.upscaling.backend)) {
    SDL_LogWarn(LogUpscaler, "%s is unavailable on the %s renderer; using Auto",
                UpscaleBackendName(settings.upscaling.backend),
                SDL_GetRendererName(renderer.get()));
    settings.upscaling.backend = UpscaleBackend::Auto;
  }
  upscaler->configure(settings.upscaling);
  audio.setMasterVolume(settings.masterVolume);
  audio.setMasterMuted(settings.masterMuted);
  for (int category : {LogCore, LogConfig, LogMedia, LogUpscaler, LogUI})
    SDL_SetLogPriority(category, settings.logPriority);
  settingsMenu = std::make_unique<SettingsMenu>(*this);
  ui = std::make_unique<UI>(settings.fontPath);
  ui->setTranslation(&translation);
  vnUI = std::make_unique<UI>(
      settings.vnFontPath.empty() ? settings.fontPath : settings.vnFontPath);
  audio.setUserVolumes(settings.channelVolumes);
  timeline = std::make_unique<TimelinePlayer>(*this);
  forcedSubtitles = std::make_unique<ForcedSubtitleRenderer>(*this);
  debug = std::make_unique<DebugOverlay>(*this);
  configureRenderSpace(RenderSpace::Novel);
  runtime.init(*this);
  if (!options.dumpTranslationPath.empty()) {
    dumpTranslation(options.dumpTranslationPath);
    translationDumpComplete = true;
  }
#if KOTONOHA_ENABLE_MCP
  if (options.enableMCP) {
    if (options.mcpPort > 0)
      mcp = std::make_unique<MCPServer>(*this, options.mcpFileRoot,
                                        options.mcpPort);
    else
      mcp = std::make_unique<MCPServer>(*this, options.mcpFileRoot);
  }
#endif
  frameClock.reset();
  SDL_LogInfo(LogCore, "Engine started (%s renderer)",
              SDL_GetRendererName(renderer.get()));
}

Engine::~Engine() {
  // The runtime or launcher may retain a shared_ptr to this asset provider.
  // Drop its renderer-owned textures while SDL_Renderer is still live; member
  // destruction alone cannot guarantee this with shared ownership.
  if (assets)
    assets->clearCache();
  if (!settingsPath.empty()) {
    try {
      saveSettings();
    } catch (const std::exception &error) {
      SDL_LogError(LogConfig, "Cannot save settings: %s", error.what());
    }
  }
}

void Engine::applyWindowSettings(const EngineSettings &value, bool geometry) {
  const auto check = [](bool result, const char *operation) {
    if (!result)
      throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
  };
  if (geometry) {
    int count = 0;
    SDLArrayPtr<SDL_DisplayID> displays(SDL_GetDisplays(&count));
    if (!displays || count <= 0)
      throw std::runtime_error("No displays are available");
    // SDL3 identifies displays by SDL_DisplayID; an index is meaningful only
    // inside this SDL_GetDisplays() snapshot. An out-of-range selection cannot
    // be resolved, so reject it instead of silently persisting a display the
    // user never selected.
    const int displayIndex = value.display;
    if (displayIndex < 0 || displayIndex >= count)
      throw std::runtime_error("Configured display is unavailable");
    const auto display = displays.get()[displayIndex];
    const auto flags = SDL_GetWindowFlags(window.get());
    const bool fullscreen = flags & SDL_WINDOW_FULLSCREEN;
    const bool wantsFullscreen = value.mode != WindowMode::Windowed;
    SDL_DisplayMode exclusive{};
    const SDL_DisplayMode *desiredMode = nullptr;
    if (value.mode == WindowMode::Exclusive) {
      if (value.fullscreenWidth > 0) {
        check(SDL_GetClosestFullscreenDisplayMode(
                  display, value.fullscreenWidth, value.fullscreenHeight,
                  value.refreshRate, true, &exclusive),
              "Find fullscreen mode");
      } else {
        const auto *desktop = SDL_GetDesktopDisplayMode(display);
        if (!desktop)
          throw std::runtime_error(SDL_GetError());
        exclusive = *desktop;
      }
      desiredMode = &exclusive;
    }
    const auto *activeMode = SDL_GetWindowFullscreenMode(window.get());
    const bool sameMode =
        (!desiredMode && !activeMode) ||
        (desiredMode && activeMode && desiredMode->w == activeMode->w &&
         desiredMode->h == activeMode->h &&
         std::abs(desiredMode->refresh_rate - activeMode->refresh_rate) <
             0.001f &&
         desiredMode->pixel_density == activeMode->pixel_density);
    const bool modeChanged =
        fullscreen != wantsFullscreen ||
        (wantsFullscreen &&
         (SDL_GetDisplayForWindow(window.get()) != display || !sameMode));
    if (fullscreen && modeChanged) {
      check(SDL_SetWindowFullscreen(window.get(), false), "Leave fullscreen");
      check(SDL_SyncWindow(window.get()), "Wait for windowed mode");
    }
    if (bool(flags & SDL_WINDOW_RESIZABLE) != value.resizable)
      check(SDL_SetWindowResizable(window.get(), value.resizable),
            "Set resizable");
    int width = 0, height = 0;
    check(SDL_GetWindowSize(window.get(), &width, &height), "Get window size");
    if ((!fullscreen || modeChanged) &&
        (width != value.width || height != value.height) &&
        canChangeDisplaySize())
      check(SDL_SetWindowSize(window.get(), value.width, value.height),
            "Set window size");
    if (SDL_GetDisplayForWindow(window.get()) != display)
      check(SDL_SetWindowPosition(window.get(),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
            "Move window to display");
    if (wantsFullscreen && modeChanged) {
      check(SDL_SetWindowFullscreenMode(window.get(), desiredMode),
            "Set fullscreen mode");
      check(SDL_SetWindowFullscreen(window.get(), true), "Enter fullscreen");
      check(SDL_SyncWindow(window.get()), "Wait for fullscreen mode");
    }
  }
  int vsync = 0;
  check(SDL_GetRenderVSync(renderer.get(), &vsync), "Get VSync");
  if (vsync != (value.vsync ? 1 : 0))
    check(SDL_SetRenderVSync(renderer.get(), value.vsync ? 1 : 0), "Set VSync");
#if defined(__ANDROID__) || defined(ANDROID)
  // Android physical refresh selection is a Window/Surface preference and is
  // not reliably represented by SDL's desktop fullscreen mode API. Re-apply
  // the persisted physical tuple after the SDL window exists.
  if (value.fullscreenWidth > 0 && value.fullscreenHeight > 0 &&
      value.refreshRate > 0.0f) {
    const auto modes = AndroidEnumerateDisplayModes();
    const auto match =
        std::find_if(modes.begin(), modes.end(), [&](const auto &mode) {
          return mode.width == value.fullscreenWidth &&
                 mode.height == value.fullscreenHeight &&
                 std::abs(mode.refreshRate - value.refreshRate) < 0.05f;
        });
    if (match != modes.end() && !AndroidRequestDisplayMode(match->modeId))
      throw std::runtime_error("Request Android display mode failed");
  }
#endif
}

void Engine::applySettings(const EngineSettings &value, bool persist) {
  value.validate();
  if (value.renderer != settings.renderer)
    throw std::invalid_argument(
        "Changing the renderer requires an engine restart");
#if KOTONOHA_ENABLE_UPSCALER
  if (!upscaler->supports(value.upscaling.backend))
    throw std::invalid_argument(
        "Upscaler is unavailable for the current renderer");
#endif
  std::unique_ptr<UI> nextUI, nextVNUI;
  if (value.fontPath != settings.fontPath) {
    nextUI = std::make_unique<UI>(value.fontPath);
    nextVNUI = std::make_unique<UI>(
        value.vnFontPath.empty() ? value.fontPath : value.vnFontPath);
  } else if (value.vnFontPath != settings.vnFontPath) {
    nextVNUI = std::make_unique<UI>(
        value.vnFontPath.empty() ? value.fontPath : value.vnFontPath);
  }
  const auto previous = settings;
  const bool geometryChanged =
      value.width != previous.width || value.height != previous.height ||
      value.display != previous.display || value.mode != previous.mode ||
      value.resizable != previous.resizable ||
      value.fullscreenWidth != previous.fullscreenWidth ||
      value.fullscreenHeight != previous.fullscreenHeight ||
      value.refreshRate != previous.refreshRate;
  try {
    if (geometryChanged || value.vsync != previous.vsync)
      applyWindowSettings(value, geometryChanged);
    upscaler->configure(value.upscaling);
    audio.setMasterVolume(value.masterVolume);
    audio.setMasterMuted(value.masterMuted);
    audio.setUserVolumes(value.channelVolumes);
    settings = value;
#if !KOTONOHA_ENABLE_UPSCALER
    settings.upscaling = {};
    settings.upscaling.backend = UpscaleBackend::None;
#endif
    if (persist)
      saveSettings();
    updateCursor(settings.customCursorPath);
  } catch (const std::exception &error) {
    const std::string failure = error.what();
    settings = previous;
    try {
      if (geometryChanged || value.vsync != previous.vsync)
        applyWindowSettings(previous, geometryChanged);
      upscaler->configure(previous.upscaling);
      audio.setMasterVolume(previous.masterVolume);
      audio.setMasterMuted(previous.masterMuted);
      audio.setUserVolumes(previous.channelVolumes);
      updateCursor(previous.customCursorPath);
    } catch (const std::exception &rollback) {
      throw std::runtime_error(
          failure + "; settings rollback failed: " + rollback.what());
    }
    throw;
  }
  if (nextVNUI)
    vnUI = std::move(nextVNUI);
  if (nextUI) {
    ui = std::move(nextUI);
    debug->reloadFonts(settings.fontPath);
  }
  for (int category : {LogCore, LogConfig, LogMedia, LogUpscaler, LogUI})
    SDL_SetLogPriority(category, settings.logPriority);
  frameClock.reset();
  SDL_LogInfo(LogConfig, "Applied window/render settings");
}

void Engine::updateCursor(const std::string &id) {
  if (id.empty()) {
    customCursor.reset();
    SDL_SetCursor(nullptr);
    return;
  }

  SurfacePtr surface = assets->loadSurface(id);
  if (!surface) {
    SDL_LogWarn(LogConfig, "Failed to load custom cursor: %s", id.c_str());
    customCursor.reset();
    SDL_SetCursor(nullptr);
    return;
  }

  SDL_Cursor *cursor = SDL_CreateColorCursor(surface.get(), 0, 0);
  if (!cursor) {
    SDL_LogWarn(LogConfig, "Failed to create custom cursor: %s",
                SDL_GetError());
    customCursor.reset();
    SDL_SetCursor(nullptr);
    return;
  }

  customCursor.reset(cursor);
  SDL_SetCursor(customCursor.get());
  SDL_LogInfo(LogConfig, "Custom cursor applied: %s", id.c_str());
}

void Engine::saveSettings() const {
  if (!settingsPath.empty())
    settings.save(settingsPath);
}

void Engine::openSettings() {
  synchronizeTimeline();
  settingsMenu->open();
  timeline->setPaused(isStoryPaused());
}

bool Engine::isStoryPaused() const {
  return agentPaused || browserMediaPlaybackBlocked() ||
         runtime.hasPausingScene() ||
         (settingsMenu && settingsMenu->isOpen()) ||
         (debug && debug->pausesStory());
}

void Engine::setAgentPaused(bool value) {
  agentPaused = value;
  timeline->setPaused(isStoryPaused());
  frameClock.reset();
}

void Engine::synchronizeTimeline() {
  const double delta = frameClock.seconds();
  frameClock.reset();
  timeline->setPaused(isStoryPaused());
  timeline->update(delta);
  synchronizeNodeClock(delta);
}

std::string Engine::activeNode() {
  if (runtime.GetState() != GameState::Gameplay)
    return {};
  auto custom = runtime.debugNode();
  if (!custom.empty())
    return custom;
  const auto &manager = GetVNManagerRuntime(*this);
  const int id = manager.getCurrentNodeId();
  return id < 0 || manager.isFinished() ? std::string() : std::to_string(id);
}

bool Engine::usesTimedNodeClock() {
  return runtime.debugNode().empty()
             ? GetVNManagerRuntime(*this).getMode() == ScriptMode::Timed
             : runtime.usesTimedNodeClock();
}

double Engine::activeNodeSeconds() {
  const auto node = activeNode();
  if (node.empty())
    return 0;
  if (usesTimedNodeClock() && !runtime.debugNode().empty()) {
    if (auto seconds = runtime.debugNodeSeconds())
      return *seconds;
    throw std::logic_error("Timed native runtime has no node clock");
  }
  if (usesTimedNodeClock()) {
    return timeline->seconds();
  }
  return activeClockNode() == clockNode ? nodeSeconds : 0;
}

double Engine::activeEventSeconds() {
  const auto node = activeNode();
  if (node.empty())
    return 0;
  if (usesTimedNodeClock()) {
    if (runtime.debugNode().empty())
      return timeline->seconds();
    if (runtime.nodeClockKey().empty()) {
      if (auto seconds = runtime.debugNodeSeconds())
        return *seconds;
      throw std::logic_error("Timed native runtime has no node clock");
    }
  }
  return node == clockEvent ? eventSeconds : 0;
}

std::string Engine::activeClockNode() {
  const auto node = activeNode();
  if (node.empty())
    return {};
  const auto key = runtime.nodeClockKey();
  return key.empty() ? node : key;
}

void Engine::resetNodeClock() {
  clockNode = activeClockNode();
  nodeSeconds = 0;
  clockEvent = activeNode();
  eventSeconds = 0;
}

void Engine::synchronizeNodeClock(double delta) {
  const auto event = activeNode();
  if (event != clockEvent) {
    clockEvent = event;
    eventSeconds = 0;
  } else if (!event.empty() && !isStoryPaused()) {
    eventSeconds += delta;
  }

  const auto node = activeClockNode();
  if (node != clockNode) {
    clockNode = node;
    nodeSeconds = 0;
  } else if (!node.empty() && !isStoryPaused()) {
    nodeSeconds += delta;
  }
}

void Engine::advanceTimeline(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 60)
    throw std::invalid_argument(
        "Timeline step must be between 0 and 60 seconds");
  if (!agentPaused || runtime.hasPausingScene() || debug->pausesStory() ||
      settingsMenu->isOpen())
    throw std::logic_error("Pause via MCP and close game/debug modals before "
                           "stepping an active timeline");
  if (runtime.advanceStoryTime(seconds)) {
    const auto event = activeNode();
    if (!event.empty() && event == clockEvent)
      eventSeconds += seconds;
    else {
      clockEvent = event;
      eventSeconds = 0;
    }
    const auto node = activeClockNode();
    if (!node.empty() && node == clockNode)
      nodeSeconds += seconds;
    else {
      clockNode = node;
      nodeSeconds = 0;
    }
    frameClock.reset();
    return;
  }
  if (!timeline->isPlaying())
    throw std::logic_error("No active timeline to step");
  timeline->setPaused(false);
  agentPaused = false;
  try {
    timeline->update(seconds);
    runtime.update();
    agentPaused = true;
    timeline->setPaused(isStoryPaused());
  } catch (...) {
    agentPaused = true;
    timeline->setPaused(isStoryPaused());
    throw;
  }
  frameClock.reset();
}

void Engine::setTranslation(const std::string &section,
                            const std::string &original,
                            const std::string &translated) {
  auto &dictionary = translation.mutableSections()[section];
  auto found = dictionary.find(original);
  auto previous = found == dictionary.end() ? original : found->second;
  if (previous == translated)
    return;
  dictionary[original] = translated;
  const int route = runtime.translationRoute();
  if (route >= 0 && section != "nodo" && section != std::to_string(route))
    return;
  for (auto *scene : runtime.GetActiveScenes())
    if (scene)
      scene->translationChanged(original, previous,
                                translated.empty() ? original : translated);
}

void Engine::SetAssetProvider(std::shared_ptr<AssetProvider> provider) {
  if (!provider)
    throw std::invalid_argument("Asset provider is required");
  if (!settings.assetOverlayPath.empty())
    provider->setOverlay(settings.assetOverlayPath);
  assets = std::move(provider);
  SDL_LogInfo(LogCore, "Asset provider: %s", assets->name().c_str());
}

std::filesystem::path Engine::ensureAssetOverlay() {
  auto root = assets->overlayDirectory();
  if (!root.empty())
    return root;
  root = documentsDirectory(runtime.identify) / "overlay";
  std::filesystem::create_directories(root);
  assets->setOverlay(root);
  constexpr const char *name = "Dubbing";
  settings.assetOverlays[name] = root.string();
  settings.assetOverlayName = name;
  settings.assetOverlayPath = root.string();
  saveSettings();
  SDL_LogInfo(LogCore, "Created engine asset overlay: %s",
              root.string().c_str());
  return assets->overlayDirectory();
}

SDL_FPoint Engine::screenSize() const {
  int width = 0, height = 0;
  if (!SDL_GetWindowSize(window.get(), &width, &height))
    throw std::runtime_error(SDL_GetError());
  return {static_cast<float>(width), static_cast<float>(height)};
}

void Engine::configureRenderSpace(RenderSpace space) {
  int pixelWidth = 0, pixelHeight = 0;
  if (!SDL_GetRenderOutputSize(renderer.get(), &pixelWidth, &pixelHeight))
    throw std::runtime_error(SDL_GetError());
  if (pixelWidth <= 0 || pixelHeight <= 0)
    return;
  const auto screen = screenSize();
  const bool novel = space == RenderSpace::Novel;
  const SDL_Point view = runtime.viewport();
  const int width = novel ? view.x : std::max(1, static_cast<int>(screen.x));
  const int height = novel ? view.y : std::max(1, static_cast<int>(screen.y));
  if (!SDL_SetRenderViewport(renderer.get(), nullptr) ||
      !SDL_SetRenderClipRect(renderer.get(), nullptr) ||
      !SDL_SetRenderScale(renderer.get(), 1, 1))
    throw std::runtime_error(SDL_GetError());
  if (!SDL_SetRenderLogicalPresentation(renderer.get(), width, height,
                                        novel
                                            ? SDL_LOGICAL_PRESENTATION_LETTERBOX
                                            : SDL_LOGICAL_PRESENTATION_STRETCH))
    throw std::runtime_error(SDL_GetError());
  SDL_FRect presentation;
  if (!SDL_GetRenderLogicalPresentationRect(renderer.get(), &presentation))
    throw std::runtime_error(SDL_GetError());
  ui->setPixelDensity(presentation.w / width);
  vnUI->setPixelDensity(presentation.w / width);
}

SDL_AppResult Engine::Event(EventPtr event) {
  if (!event)
    return SDL_APP_CONTINUE;
  // Keep the A/B preference at the outer edge so every component and runtime
  // sees the same physical layout. Recursive navigation events only carry the
  // D-pad, which the swap deliberately leaves untouched.
  const SDL_Event normalized =
      NormalizeControllerEvent(*event, settings.swapControllerAB);
  event = &normalized;
  session.deviceEvent(*event);
  const Uint64 nowMs = SDL_GetTicks();
  const auto navigation = controllerNavigation.process(*event);
  // Synthetic D-pad releases must also reach the runtime when a controller
  // disconnects or the window loses focus, not only on axis motion.
  for (auto button : navigation) {
    const auto result = Event(&button);
    if (result != SDL_APP_CONTINUE)
      return result;
  }
  if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
      (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX ||
       event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY))
    return SDL_APP_CONTINUE;
  navRepeater.process(*event, nowMs);
  inputActions.process(*event, nowMs);
  SDL_Event pointer{};
  const bool nativeTouch = runtime.continuousTouch() || settingsMenu->isOpen();
  const bool pointerEvent = touchPointer.translate(*event, *window, pointer);
  const bool touchGesture =
      event->type >= SDL_EVENT_FINGER_DOWN &&
      event->type <= SDL_EVENT_FINGER_CANCELED &&
      (inputActions.back(*event) || inputActions.backlog(*event));
  bool translate = nativeTouch && pointerEvent;
  if (nativeTouch && touchGesture)
    translate = touchPointer.cancel(*window, pointer) || translate;
  const auto result = dispatchEvent(event);
  if (result == SDL_APP_CONTINUE && !nativeTouch &&
      event->type >= SDL_EVENT_FINGER_DOWN &&
      event->type <= SDL_EVENT_FINGER_CANCELED && inputActions.skip(*event)) {
    SDL_Event skipKey{};
    skipKey.type = SDL_EVENT_KEY_DOWN;
    skipKey.key.key = SDLK_S;
    skipKey.key.down = true;
    dispatchEvent(&skipKey);
  }
  if (translate && result == SDL_APP_CONTINUE)
    return dispatchEvent(&pointer);
  return result;
}
SDL_AppResult Engine::dispatchEvent(EventPtr event) {
  if (!event)
    return SDL_APP_CONTINUE;
  if (event->type == SDL_EVENT_QUIT)
    return SDL_APP_SUCCESS;
  const bool synthesizeTouch =
      !runtime.continuousTouch() && !settingsMenu->isOpen();
  runtime.devInputProcessed(*event);
  synchronizeTimeline();
  const bool debugConsumed = debug->handleEvent(*event);
  if (debugConsumed) {
    timeline->setPaused(isStoryPaused());
    return SDL_APP_CONTINUE;
  }
  configureRenderSpace(RenderSpace::Screen);
  SDL_Event screenEvent = *event;
  if (!SDL_ConvertEventToRenderCoordinates(renderer.get(), &screenEvent))
    throw std::runtime_error(SDL_GetError());
  bool consumed = !settingsMenu->isOpen() && runtime.onScreenEvent(screenEvent);
  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    const Uint32 buttonMask = SDL_BUTTON_MASK(event->button.button);
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (consumed)
        screenCapturedMouseButtons |= buttonMask;
    } else {
      consumed = consumed || (screenCapturedMouseButtons & buttonMask) != 0;
      screenCapturedMouseButtons &= ~buttonMask;
    }
  }
  const bool touchTap = inputActions.tap();
  configureRenderSpace(RenderSpace::Novel);
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
      (event->key.key == SDLK_F11 ||
       (event->key.key == SDLK_RETURN && (event->key.mod & SDL_KMOD_ALT)))) {
    auto value = settings;
    value.mode = value.mode == WindowMode::Windowed ? WindowMode::Borderless
                                                    : WindowMode::Windowed;
    applySettings(value);
    return SDL_APP_CONTINUE;
  }
  if (event->type == SDL_EVENT_WINDOW_RESIZED &&
      settings.mode == WindowMode::Windowed && event->window.data1 >= 320 &&
      event->window.data2 >= 240) {
    settings.width = event->window.data1;
    settings.height = event->window.data2;
  }
  SDL_Event logical = *event;
  if (!SDL_ConvertEventToRenderCoordinates(renderer.get(), &logical))
    throw std::runtime_error(SDL_GetError());
  // A game can letterbox/compress only its authored Novel frame without
  // changing the native Screen controls or the engine-wide SettingsMenu.
  // Invert that transform for story/title input so hotspots still align with
  // their pixels after the texture is composed into a shorter rectangle.
  if (!consumed && !settingsMenu->isOpen()) {
    const float novelHeightScale = runtime.narrativePresentationHeightScale();
    if (novelHeightScale > 0.0f && novelHeightScale < 1.0f) {
      const SDL_Point view = runtime.viewport();
      const float yInset = view.y * (1.0f - novelHeightScale) * 0.5f;
      if (logical.type == SDL_EVENT_MOUSE_MOTION)
        logical.motion.y = (logical.motion.y - yInset) / novelHeightScale;
      else if (logical.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
               logical.type == SDL_EVENT_MOUSE_BUTTON_UP)
        logical.button.y = (logical.button.y - yInset) / novelHeightScale;
    }
  }
  const SDL_Point view = runtime.viewport();
  const auto frame = UI::designFrame(view);
  SDL_Event framed = UI::toFrame(logical, frame);

  // Manual mobile menu button click detection
  const SDL_FRect mobileMenuButton{16, 16, 76, 36};
  if (isMobilePlatform() && runtime.GetState() == GameState::Gameplay &&
      !settingsMenu->isOpen() && !GetInGameMenuRuntime(*this).IsOpen()) {
    if (logical.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        logical.button.button == SDL_BUTTON_LEFT) {
      const SDL_FPoint pt{framed.button.x, framed.button.y};
      if (SDL_PointInRectFloat(&pt, &mobileMenuButton)) {
        GetInGameMenuRuntime(*this).open();
        return SDL_APP_CONTINUE;
      }
    } else if (logical.type == SDL_EVENT_FINGER_DOWN) {
      const SDL_FPoint pt{logical.tfinger.x * view.x,
                          logical.tfinger.y * view.y};
      const SDL_FPoint designPt{(pt.x - frame.x) / frame.scale,
                                (pt.y - frame.y) / frame.scale};
      if (SDL_PointInRectFloat(&designPt, &mobileMenuButton)) {
        GetInGameMenuRuntime(*this).open();
        return SDL_APP_CONTINUE;
      }
    }
  }

  if (!consumed && !settingsMenu->isOpen() && HandleNavigation(*this, logical))
    return SDL_APP_CONTINUE;
  const bool outsideNovel =
      logical.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      (logical.button.x < 0 || logical.button.x >= view.x ||
       logical.button.y < 0 || logical.button.y >= view.y);
  if (settingsMenu->isOpen()) {
    // The click that launches Settings from a game's native panel owns its
    // release as well. Do not reinterpret that release as a tap on an engine
    // tab underneath the same screen coordinate (e.g. SOMCON -> VN text).
    if (!consumed)
      settingsMenu->onEvent(UI::toFrame(logical, UI::designFrame(view)));
  } else if (!consumed && !outsideNovel)
    runtime.onEvent(logical);
  const bool wasPaused = timeline->isPaused();
  timeline->setPaused(isStoryPaused());
  if (wasPaused != timeline->isPaused())
    frameClock.reset();
  if (touchTap && !consumed && synthesizeTouch &&
      event->type == SDL_EVENT_FINGER_UP) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window.get(), &w, &h);
    SDL_Event tap{};
    tap.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    tap.button.windowID = SDL_GetWindowID(window.get());
    tap.button.which = SDL_TOUCH_MOUSEID;
    tap.button.button = SDL_BUTTON_LEFT;
    tap.button.down = true;
    tap.button.x = event->tfinger.x * w;
    tap.button.y = event->tfinger.y * h;
    dispatchEvent(&tap);
    tap.type = SDL_EVENT_MOUSE_BUTTON_UP;
    tap.button.down = false;
    dispatchEvent(&tap);
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult Engine::Iterate() {
  const char *stage = "frame setup";
  try {
    if (translationDumpComplete)
      return SDL_APP_SUCCESS;
    const Uint64 nowMs = SDL_GetTicks();
    for (auto repeated : navRepeater.update(nowMs)) {
      const auto repeatResult = dispatchEvent(&repeated);
      if (repeatResult != SDL_APP_CONTINUE)
        return repeatResult;
    }
    if (inputActions.pollLongPress(nowMs)) {
      SDL_Event cancelPtr{};
      if (touchPointer.cancel(*window, cancelPtr))
        dispatchEvent(&cancelPtr);
      SDL_Event menuKey{};
      menuKey.type = SDL_EVENT_KEY_DOWN;
      menuKey.key.key = SDLK_ESCAPE;
      menuKey.key.down = true;
      const auto longPressResult = dispatchEvent(&menuKey);
      if (longPressResult != SDL_APP_CONTINUE)
        return longPressResult;
    }
    configureRenderSpace(RenderSpace::Novel);
    synchronizeTimeline();
#if KOTONOHA_ENABLE_MCP
    if (mcp && !mcp->pump())
      return SDL_APP_SUCCESS;
#endif
    // A browser media-control pause must also stop game logic, not just the
    // timeline clock. Continue rendering so native browser controls remain
    // usable; resume automatically as soon as browser playback resumes.
    if (browserMediaPlaybackBlocked()) {
      stage = "runtime idle";
      runtime.devIdle();
    } else if (!runtime.devPauseRequested() || runtime.devConsumeStep()) {
      stage = "runtime update";
      runtime.update();
    } else {
      stage = "runtime idle";
      runtime.devIdle();
    }
    stage = "frame preparation";
    synchronizeNodeClock(0);
    forcedSubtitles->updateForActiveNode();
    debug->update();

    // Clear the native output first so the letterbox outside the fitted Novel
    // presentation remains deterministic even when the Novel itself is rendered
    // into a reduced intermediate target.
    SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 0);
    SDL_RenderClear(renderer.get());

    const SDL_Point novelView = runtime.viewport();
    // Prevent low-resolution intermediate targets from enlarging TTF glyphs.
    const float novelScale =
        runtime.requiresNativeTextResolution() ? 1.0f : settings.renderScale;
    const bool scaledNovel = novelScale < 0.9999f;
    const bool ownedNovel = scaledNovel || runtime.useOwnedNarrativeTarget();
    if (ownedNovel) {
      SDL_FRect presentation{};
      if (!SDL_GetRenderLogicalPresentationRect(renderer.get(), &presentation))
        throw std::runtime_error(SDL_GetError());
      const int targetWidth = std::max(
          1, static_cast<int>(std::lround(presentation.w * novelScale)));
      const int targetHeight = std::max(
          1, static_cast<int>(std::lround(presentation.h * novelScale)));
      if (!novelTarget || novelTargetWidth != targetWidth ||
          novelTargetHeight != targetHeight) {
        TexturePtr next(SDL_CreateTexture(
            renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
            targetWidth, targetHeight));
        if (!next ||
            !SDL_SetTextureScaleMode(next.get(), SDL_SCALEMODE_LINEAR) ||
            !SDL_SetTextureBlendMode(next.get(), SDL_BLENDMODE_NONE))
          throw std::runtime_error(SDL_GetError());
        novelTarget = std::move(next);
        novelTargetWidth = targetWidth;
        novelTargetHeight = targetHeight;
      }
      if (!SDL_SetRenderTarget(renderer.get(), novelTarget.get()) ||
          !SDL_SetRenderViewport(renderer.get(), nullptr) ||
          !SDL_SetRenderClipRect(renderer.get(), nullptr) ||
          !SDL_SetRenderScale(renderer.get(), 1, 1) ||
          !SDL_SetRenderLogicalPresentation(renderer.get(), novelView.x,
                                            novelView.y,
                                            SDL_LOGICAL_PRESENTATION_STRETCH))
        throw std::runtime_error(SDL_GetError());
      ui->setPixelDensity(
          std::min(static_cast<float>(targetWidth) / novelView.x,
                   static_cast<float>(targetHeight) / novelView.y));
      SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 0);
      if (!SDL_RenderClear(renderer.get()))
        throw std::runtime_error(SDL_GetError());
    }

    stage = "background render";
    runtime.renderBackground(*renderer, ui->pixelDensity());
    const auto design = UI::designFrame(runtime.viewport());
    {
      UI::FrameScope scope(*renderer, design);
      timeline->renderVideo(*renderer);
      timeline->renderSubtitles(*renderer);
    }
    stage = "scene render";
    runtime.render(*renderer, ui->pixelDensity());
    runtime.renderScene(*forcedSubtitles, *renderer, ui->pixelDensity());
    stage = "overlay render";
    runtime.renderOverlay(*renderer, ui->pixelDensity());
    if (ownedNovel) {
      if (runtime.useOwnedNarrativeTarget() &&
          !visualTransition.compose(
              *renderer, *novelTarget, runtime.narrativeFrameDrawable(),
              runtime.narrativeBoundaryFrame(), runtime.narrativeVisualOwner()))
        throw std::runtime_error(SDL_GetError());
      // VisualTransition::compose/copy temporarily disables logical
      // presentation for 1:1 texture snapshots. Restore the Novel logical
      // presentation before renderNarrativeOverlay so narrative overlays fill
      // the complete Novel viewport at every resolution and renderScale.
      if (!SDL_SetRenderTarget(renderer.get(), novelTarget.get()) ||
          !SDL_SetRenderViewport(renderer.get(), nullptr) ||
          !SDL_SetRenderClipRect(renderer.get(), nullptr) ||
          !SDL_SetRenderScale(renderer.get(), 1, 1) ||
          !SDL_SetRenderLogicalPresentation(renderer.get(), novelView.x,
                                            novelView.y,
                                            SDL_LOGICAL_PRESENTATION_STRETCH))
        throw std::runtime_error(SDL_GetError());
      runtime.renderNarrativeOverlay(*renderer, ui->pixelDensity());
      if (!SDL_SetRenderTarget(renderer.get(), nullptr))
        throw std::runtime_error(SDL_GetError());
      configureRenderSpace(RenderSpace::Novel);
      // The media/image upscalers have already run at their existing call
      // sites. This copy only maps the reduced Novel raster back to the fitted
      // physical presentation, avoiding a second upscaler pass over the whole
      // frame.
      const float heightScale = runtime.narrativePresentationHeightScale();
      if (!std::isfinite(heightScale) || heightScale <= 0.0f ||
          heightScale > 1.0f)
        throw std::invalid_argument("Invalid Novel presentation height scale");
      const SDL_FRect presentedNovel{
          0.0f, novelView.y * (1.0f - heightScale) * 0.5f,
          static_cast<float>(novelView.x), novelView.y * heightScale};
      if (!SDL_RenderTexture(renderer.get(), novelTarget.get(), nullptr,
                             &presentedNovel))
        throw std::runtime_error(SDL_GetError());
    }
    configureRenderSpace(RenderSpace::Screen);
    stage = "screen render";
    runtime.renderScreen(*renderer, ui->pixelDensity());
    if (settingsMenu->isOpen()) {
      configureRenderSpace(RenderSpace::Novel);
      {
        UI::FrameScope scope(*renderer, design);
        settingsMenu->render(*renderer);
      }
      configureRenderSpace(RenderSpace::Screen);
    }
    debug->renderHUD(*renderer);
    debug->renderMenu(*renderer);
    runtime.devControlPump(*renderer);
    SDL_RenderPresent(renderer.get());
    configureRenderSpace(RenderSpace::Novel);
    return SDL_APP_CONTINUE;
  } catch (const std::bad_alloc &) {
    throw std::runtime_error(std::string("Memory allocation failed during ") +
                             stage);
  }
}

void Engine::dumpTranslation(const std::string &path) {
  auto sources = runtime.translationSources();
  for (auto &[route, texts] : GetVNManagerRuntime(*this).translationSources()) {
    auto &target = sources[route];
    target.insert(target.end(), texts.begin(), texts.end());
  }
  if (sources.empty())
    throw std::logic_error(
        "No texts to export: load a script or start a game with text routes");
  translation.dump(path, sources);
}

} // namespace Kotonoha
