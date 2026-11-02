#pragma once

#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha {

class Engine;
class AudioChannel;
struct DebugNode;
namespace ImGuiTheme {
class ShaderFilePicker;
}

struct DebugSettings {
  bool showFPS = true;
  bool showNodeClock = true;
};

class DebugOverlay {
  Engine &engine;
  DebugSettings settings;
  bool visible = false;
  Clock frameClock;
  double elapsed = 0, frames = 0, fps = 0;
  double runtimeNodeSeconds = 0;
  std::string runtimeNode;
  float contentScale = 0;
  float upscaleFactor = 1;
  int selectedNode = -1;
  char exportPath[1024] = "translation.json";
  std::string status;
  bool statusError = false;
  char logFilter[128]{};
  int minimumLogPriority = SDL_LOG_PRIORITY_INFO;
  bool followLogs = true;
  std::unique_ptr<ImGuiTheme::ShaderFilePicker> shaderPicker;
  char ktrfPath[1024] = "story.ktnroute";
  std::string selectedRuntimeNode;
  // Node lists are expensive (whole KTRF graphs); refreshed on demand only.
  std::vector<DebugNode> cachedRuntimeNodes, cachedEngineNodes;
  bool nodesStale = true;
  void refreshNodes();
  char assetPath[1024]{};
  std::string previewAsset;
  std::string previewError;
  float previewSeconds = 0;
  TexturePtr previewTexture;
  AudioStreamPtr previewAudio;
  std::shared_ptr<AudioChannel> previewChannel;
  std::vector<std::string> browsedAssets;
  bool showTranslationWindow = false;
  std::string selectedTransNodeStr;
  char transSearchFilter[128]{};
  Translation::SourceTexts cachedManagerSources;
  Translation::SourceTexts cachedRuntimeSources;
  std::vector<std::string> cachedAllNodeKeys;
  bool transSourcesStale = true;
  void refreshTranslationSources();
  void drawKtrfTools();
  void drawRuntimeNodes(const std::vector<DebugNode> &nodes);
  void drawAssets(const std::vector<std::string> &assets);
  void stopPreviewAudio();
  void drawInspector();
  void drawTranslationExport();
  void drawTranslationEditor();
  void drawTranslationWindow();
  void drawEngineTools();

public:
  explicit DebugOverlay(Engine &engine);
  ~DebugOverlay();
  DebugOverlay(const DebugOverlay &) = delete;
  DebugOverlay &operator=(const DebugOverlay &) = delete;

  bool handleEvent(EventRef event);
  void update();
  void renderHUD(RendererRef renderer);
  void renderMenu(RendererRef renderer);
  bool isVisible() const { return visible || showTranslationWindow; }
  void setVisible(bool value);
  DebugSettings &getSettings() { return settings; }
  const DebugSettings &getSettings() const { return settings; }
  std::string nodeClockText() const;
  std::string performanceText() const;
};

} // namespace Kotonoha
