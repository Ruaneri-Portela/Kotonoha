#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/media/AudioMixer.hpp>
#include <Kotonoha/parsers/AssStyle.hpp>

#include <memory>
#include <string>

namespace Kotonoha {

class UI;
class TimelinePlayer;
class DebugOverlay;
class MCPServer;
class SettingsMenu;

struct EngineOptions {
  std::string translationPath;
  bool enableMCP = false;
  std::string mcpFileRoot = ".";
  EngineSettings settings;
  std::string settingsPath;
  bool persistSettings = false;
  std::string logPath;
  std::string rendererOverride;
  std::string subtitleStylePath;
  // Asset source; defaults to plain files relative to the working directory.
  std::shared_ptr<AssetProvider> assets;
};

class Engine {
private:
  Runtime &runtime;
  Log log;
  SDLSession session;

  WindowPtr window;
  RendererPtr renderer;
  // Declared after the renderer so cached textures are released first.
  std::shared_ptr<AssetProvider> assets;
  std::unique_ptr<Upscaling> upscaler;
  EngineSettings settings;
  std::string settingsPath;
  std::optional<AssStyle> subtitleStyle;
  std::unique_ptr<SettingsMenu> settingsMenu;

  std::unique_ptr<UI> ui;
  AudioMixer audio;
  std::unique_ptr<TimelinePlayer> timeline;
  std::unique_ptr<DebugOverlay> debug;
  std::unique_ptr<MCPServer> mcp;
  bool agentPaused = false;
  Clock frameClock;
  Translation translation;
  void configureRenderSpace(RenderSpace space);
  void applyWindowSettings(const EngineSettings &value);

public:
  explicit Engine(Runtime &run, EngineOptions options = {});
  ~Engine();

  WindowRef GetWindow() const { return *window; }
  Runtime &GetRuntime() { return runtime; }
  UI &GetUI() { return *ui; }
  TimelinePlayer &GetTimeline() { return *timeline; }
  RendererRef GetRenderer() { return *renderer; }
  SDL_FPoint screenSize() const;
  const Translation &GetTranslation() const { return translation; }
  // Writes every original text (runtime routes + loaded script nodes) to a
  // translation JSON, keeping the translations already loaded.
  void dumpTranslation(const std::string &path);
  std::vector<DebugNode> debugNodes();
  void debugJumpToNode(const std::string &id);
  void loadKtrf(const std::string &path);
  void dumpKtrf(const std::string &path);
  DebugOverlay &GetDebug() { return *debug; }
  Upscaling &GetUpscaler() { return *upscaler; }
  AudioMixer &GetAudio() { return audio; }
  AssetProvider &GetAssets() { return *assets; }
  void SetAssetProvider(std::shared_ptr<AssetProvider> provider);
  const EngineSettings &GetSettings() const { return settings; }
  const std::optional<AssStyle> &GetSubtitleStyle() const { return subtitleStyle; }
  void applySettings(const EngineSettings &value, bool persist = true);
  void saveSettings() const;
  void openSettings();
  SettingsMenu &GetSettingsMenu() { return *settingsMenu; }
  bool isStoryPaused() const;
  void setAgentPaused(bool value);
  void synchronizeTimeline();
  void advanceTimeline(double seconds);

  SDL_AppResult Event(EventPtr event);
  SDL_AppResult Iterate();
};

} // namespace Kotonoha