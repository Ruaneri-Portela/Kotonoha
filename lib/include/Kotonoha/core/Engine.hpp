#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/Runtime.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/core/Settings.hpp>
#include <Kotonoha/core/VisualTransition.hpp>
#include <Kotonoha/events/ControllerEvent.hpp>
#include <Kotonoha/events/InputActions.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <Kotonoha/media/AudioMixer.hpp>
#include <Kotonoha/parsers/AssStyle.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace Kotonoha {

class UI;
class TimelinePlayer;
class DebugOverlay;
class MCPServer;
class SettingsMenu;
class ForcedSubtitleRenderer;

struct EngineOptions {
  std::string translationPath;
  std::string assetOverlayPath;
  bool assetOverlaySpecified = false;
  std::string assetOverlayDirectory() const;
  bool enableMCP = false;
  std::string mcpFileRoot = ".";
  EngineSettings settings;
  std::string settingsPath;
  bool persistSettings = false;
  std::string logPath;
  std::string rendererOverride;
  std::string subtitleStylePath;
  bool launcher = defaultLauncherEnabled();
  std::string dumpTranslationPath;
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
  TexturePtr novelTarget;
  int novelTargetWidth = 0, novelTargetHeight = 0;
  VisualTransition visualTransition;
  // Declared after the renderer so cached textures are released first.
  std::shared_ptr<AssetProvider> assets;
  std::unique_ptr<Upscaling> upscaler;
  EngineSettings settings;
  std::string settingsPath;
  bool settingsLoadedFromDisk = false;
  std::optional<AssStyle> subtitleStyle;
  std::unique_ptr<SettingsMenu> settingsMenu;

  std::unique_ptr<UI> ui;
  std::unique_ptr<UI> vnUI;
  AudioMixer audio;
  std::unique_ptr<TimelinePlayer> timeline;
  std::unique_ptr<ForcedSubtitleRenderer> forcedSubtitles;
  std::string clockNode;
  double nodeSeconds = 0;
  std::string clockEvent;
  double eventSeconds = 0;
  std::string activeClockNode();
  void synchronizeNodeClock(double delta);
  std::unique_ptr<DebugOverlay> debug;
  std::unique_ptr<MCPServer> mcp;
  bool agentPaused = false;
  bool translationDumpComplete = false;
  InputActions inputActions;
  TouchPointer touchPointer;
  ControllerNavigation controllerNavigation;
  SDL_AppResult dispatchEvent(EventPtr event);
  // A screen-space control owns both edges of a mouse press, even if the
  // pressed edge opens a modal before the release event arrives.
  Uint32 screenCapturedMouseButtons = 0;
  Clock frameClock;
  Translation translation;
  std::unordered_map<std::type_index, std::unique_ptr<Scene>> sharedScenes;
  float edgeSwipeStartX = -1;
  bool edgeSwipeActive = false;
  void configureRenderSpace(RenderSpace space);
  void applyWindowSettings(const EngineSettings &value, bool geometry = true);

public:
  explicit Engine(Runtime &run, EngineOptions options = {});
  ~Engine();

  WindowRef GetWindow() const { return *window; }
  Runtime &GetRuntime() { return runtime; }
  std::size_t GamepadCount() const { return session.gamepadCount(); }
  template <class T, class... Args> T &sharedScene(Args &&...args) {
    const std::type_index key(typeid(T));
    auto found = sharedScenes.find(key);
    if (found == sharedScenes.end()) {
      auto scene = std::make_unique<T>(*this, std::forward<Args>(args)...);
      found = sharedScenes.emplace(key, std::move(scene)).first;
    }
    return static_cast<T &>(*found->second);
  }
  // Install a native component before its generic helper is first used.
  // Reusing the same implementation is safe; replacing a live component is
  // rejected because scenes and callbacks may already hold its address.
  template <class Base, class Native, class... Args>
  Native &configureSharedScene(Args &&...args) {
    static_assert(std::is_base_of_v<Base, Native>);
    const std::type_index key(typeid(Base));
    if (auto found = sharedScenes.find(key); found != sharedScenes.end()) {
      if (auto *native = dynamic_cast<Native *>(found->second.get()))
        return *native;
      throw std::logic_error(
          "Shared component already has another implementation");
    }
    auto component =
        std::make_unique<Native>(*this, std::forward<Args>(args)...);
    auto *native = component.get();
    if (!sharedScenes.emplace(key, std::move(component)).second)
      throw std::logic_error("Recursive shared component construction");
    return *native;
  }
  UI &GetUI() { return *ui; }
  UI &GetVNUI() { return *vnUI; }
  TimelinePlayer &GetTimeline() { return *timeline; }
  ForcedSubtitleRenderer &GetForcedSubtitles() { return *forcedSubtitles; }
  std::string activeNode();
  bool usesTimedNodeClock();
  double activeEventSeconds();
  double activeNodeSeconds();
  void resetNodeClock();
  InputActions &GetInputActions() { return inputActions; }
  RendererRef GetRenderer() { return *renderer; }
  SDL_Texture *GetNovelTarget() const { return novelTarget.get(); }
  SDL_FPoint screenSize() const;
  const Translation &GetTranslation() const { return translation; }
  void setTranslation(const std::string &section, const std::string &original,
                      const std::string &translated);
  Translation &GetTranslation() { return translation; }
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
  VisualTransition &GetVisualTransition() { return visualTransition; }
  bool HasStoredSettings() const { return settingsLoadedFromDisk; }
  const std::optional<AssStyle> &GetSubtitleStyle() const { return subtitleStyle; }
  void applySettings(const EngineSettings &value, bool persist = true);
  void saveSettings() const;
  void openSettings();
  SettingsMenu &GetSettingsMenu() { return *settingsMenu; }
  bool isStoryPaused() const;
  void setAgentPaused(bool value);
  bool isAgentPaused() const { return agentPaused; }
  void synchronizeTimeline();
  void advanceTimeline(double seconds);

  SDL_AppResult Event(EventPtr event);
  SDL_AppResult Iterate();
};

} // namespace Kotonoha
