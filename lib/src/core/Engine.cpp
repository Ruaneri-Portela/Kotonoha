#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/mcp/MCPServer.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace Kotonoha {

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
      if (!asset.empty()) item.assets.push_back(asset);
    for (const auto &cue : node.timeline.cues)
      if ((cue.type == CueType::Video || cue.type == CueType::Audio) &&
          !cue.value.empty())
        item.assets.push_back(cue.value);
    result.push_back(std::move(item));
  }
  return result;
}

void Engine::debugJumpToNode(const std::string &id) {
  if (runtime.GetState() != GameState::Gameplay)
    throw std::logic_error("Start gameplay before jumping to a node");
  if (settingsMenu->isOpen())
    throw std::logic_error("Close engine settings before jumping to a node");
  if (!runtime.debugNodes().empty()) {
    runtime.debugJumpToNode(id);
  } else {
    int number = -1;
    const auto parsed = std::from_chars(id.data(), id.data() + id.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size())
      throw std::invalid_argument("Invalid script node ID");
    GetVNManagerRuntime(*this).debugJumpToNode(number);
  }
}

void Engine::loadKtrf(const std::string &path) {
  if (settingsMenu->isOpen())
    throw std::logic_error("Close engine settings before loading KTRF");
  if (runtime.hasCustomKtrf()) {
    runtime.loadKtrf(path);
  } else {
    if (!runtime.debugNodes().empty())
      throw std::logic_error("This game's native scripts do not use the VN KTRF profile");
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
      throw std::logic_error("This game's native scripts do not use the VN KTRF profile");
    GetVNManagerRuntime(*this).dumpKtrf(path);
  }
  SDL_LogInfo(LogCore, "Dumped KTRF: %s", path.c_str());
}

SDLSession::SDLSession(const Runtime &runtime) {
  if (!SDL_SetAppMetadata(runtime.name.c_str(), runtime.version.c_str(),
                          runtime.identify.c_str()) ||
      !SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    throw std::runtime_error(SDL_GetError());
}

SDLSession::~SDLSession() { SDL_Quit(); }

Engine::Engine(Runtime &run, EngineOptions options)
    : runtime(run), log(options.logPath), session(runtime),
      assets(options.assets ? std::move(options.assets)
                            : std::make_shared<FileSystemAssetProvider>(
#ifdef __ANDROID__
                                  resourceDirectory()
#else
                                  std::filesystem::path{}
#endif
                                  )),
      settings(options.settings), settingsPath(options.settingsPath) {
  if (!options.subtitleStylePath.empty())
    subtitleStyle = AssStyle::load(options.subtitleStylePath);
  if (settingsPath.empty() && options.persistSettings)
    settingsPath = EngineSettings::defaultPath(runtime.identify);
  const bool storedSettings =
      !settingsPath.empty() && std::filesystem::exists(settingsPath);
  if (storedSettings)
    settings = EngineSettings::load(settingsPath);
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
  settings.validate();
  if (options.translationPath.empty()) {
    if (!settings.translationPath.empty())
      options.translationPath = settings.translationPath;
    else if (const char *path = SDL_getenv("KOTONOHA_TRANSLATION"))
      options.translationPath = path;
  }
  if (!options.translationPath.empty())
    translation.load(options.translationPath);
  window.reset(
      SDL_CreateWindow(runtime.name.c_str(), settings.width, settings.height,
                       (settings.resizable ? SDL_WINDOW_RESIZABLE : 0) |
                           SDL_WINDOW_HIGH_PIXEL_DENSITY));
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
  timeline = std::make_unique<TimelinePlayer>(*this);
  debug = std::make_unique<DebugOverlay>(*this);
  configureRenderSpace(RenderSpace::Novel);
  runtime.init(*this);
  if (options.enableMCP)
    mcp = std::make_unique<MCPServer>(*this, options.mcpFileRoot);
  frameClock.reset();
  SDL_LogInfo(LogCore, "Engine started (%s renderer)",
              SDL_GetRendererName(renderer.get()));
}

Engine::~Engine() {
  if (!settingsPath.empty()) {
    try {
      saveSettings();
    } catch (const std::exception &error) {
      SDL_LogError(LogConfig, "Cannot save settings: %s", error.what());
    }
  }
}

void Engine::applyWindowSettings(const EngineSettings &value) {
  int count = 0;
  SDL_DisplayID *raw = SDL_GetDisplays(&count);
  std::unique_ptr<SDL_DisplayID, decltype(&SDL_free)> displays(raw, SDL_free);
  if (!displays || value.display >= count)
    throw std::runtime_error("Configured display is unavailable");
  const auto display = displays.get()[value.display];
  const auto check = [](bool result, const char *operation) {
    if (!result)
      throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
  };
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
      (width != value.width || height != value.height))
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
  int vsync = 0;
  check(SDL_GetRenderVSync(renderer.get(), &vsync), "Get VSync");
  if (vsync != (value.vsync ? 1 : 0))
    check(SDL_SetRenderVSync(renderer.get(), value.vsync ? 1 : 0), "Set VSync");
}

void Engine::applySettings(const EngineSettings &value, bool persist) {
  value.validate();
  if (value.renderer != settings.renderer)
    throw std::invalid_argument(
        "Changing the renderer requires an engine restart");
  if (!upscaler->supports(value.upscaling.backend))
    throw std::invalid_argument(
        "Upscaler is unavailable for the current renderer");
  const auto previous = settings;
  try {
    applyWindowSettings(value);
    upscaler->configure(value.upscaling);
    audio.setMasterVolume(value.masterVolume);
    audio.setMasterMuted(value.masterMuted);
    settings = value;
    if (persist)
      saveSettings();
  } catch (const std::exception &error) {
    const std::string failure = error.what();
    settings = previous;
    try {
      applyWindowSettings(previous);
      upscaler->configure(previous.upscaling);
      audio.setMasterVolume(previous.masterVolume);
      audio.setMasterMuted(previous.masterMuted);
    } catch (const std::exception &rollback) {
      throw std::runtime_error(
          failure + "; settings rollback failed: " + rollback.what());
    }
    throw;
  }
  for (int category : {LogCore, LogConfig, LogMedia, LogUpscaler, LogUI})
    SDL_SetLogPriority(category, settings.logPriority);
  frameClock.reset();
  SDL_LogInfo(LogConfig, "Applied window/render settings");
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
  return agentPaused || runtime.isStoryPaused() ||
         (settingsMenu && settingsMenu->isOpen()) ||
         (debug && debug->isVisible());
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
}

void Engine::advanceTimeline(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 60)
    throw std::invalid_argument(
        "Timeline step must be between 0 and 60 seconds");
  if (!agentPaused || runtime.isStoryPaused() || debug->isVisible() ||
      settingsMenu->isOpen() || !timeline->isPlaying())
    throw std::logic_error("Pause via MCP and close game/debug modals before "
                           "stepping an active timeline");
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

void Engine::SetAssetProvider(std::shared_ptr<AssetProvider> provider) {
  if (!provider)
    throw std::invalid_argument("Asset provider is required");
  assets = std::move(provider);
  SDL_LogInfo(LogCore, "Asset provider: %s", assets->name().c_str());
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
}

SDL_AppResult Engine::Event(EventPtr event) {
  if (!event)
    return SDL_APP_CONTINUE;
  if (event->type == SDL_EVENT_QUIT)
    return SDL_APP_SUCCESS;
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
  const bool consumed =
      !settingsMenu->isOpen() && runtime.onScreenEvent(screenEvent);
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
  const bool mouseButton = logical.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                           logical.type == SDL_EVENT_MOUSE_BUTTON_UP;
  const SDL_Point view = runtime.viewport();
  const bool outsideNovel =
      mouseButton && (logical.button.x < 0 || logical.button.x >= view.x ||
                      logical.button.y < 0 || logical.button.y >= view.y);
  if (settingsMenu->isOpen())
    settingsMenu->onEvent(UI::toFrame(logical, UI::designFrame(view)));
  else if (!consumed && !outsideNovel)
    runtime.onEvent(logical);
  const bool wasPaused = timeline->isPaused();
  timeline->setPaused(isStoryPaused());
  if (wasPaused != timeline->isPaused())
    frameClock.reset();
  return SDL_APP_CONTINUE;
}

SDL_AppResult Engine::Iterate() {
  configureRenderSpace(RenderSpace::Novel);
  synchronizeTimeline();
  if (mcp && !mcp->pump())
    return SDL_APP_SUCCESS;
  runtime.update();
  debug->update();
  SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 0);
  SDL_RenderClear(renderer.get());
  runtime.renderBackground(*renderer, ui->pixelDensity());
  const auto design = UI::designFrame(runtime.viewport());
  {
    UI::FrameScope scope(*renderer, design);
    timeline->renderVideo(*renderer);
    timeline->renderSubtitles(*renderer);
  }
  runtime.render(*renderer, ui->pixelDensity());
  configureRenderSpace(RenderSpace::Screen);
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
  SDL_RenderPresent(renderer.get());
  configureRenderSpace(RenderSpace::Novel);
  return SDL_APP_CONTINUE;
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
