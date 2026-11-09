#pragma once
#include <Kotonoha/events/InputActions.hpp>

#include <Kotonoha/core/Clock.hpp>
#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/localization/Translation.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Kotonoha {

class Engine;
class AudioChannel;
class VoiceDubbingApplet;
struct DebugNode;
namespace ImGuiTheme {
class ShaderFilePicker;
}

struct DebugSettings {
  bool showFPS = true;
  bool showNodeClock = true;
  bool ignorePause = false;
  std::string fpsLabel = "FPS";
  std::string nodeLabel = "Node";
  std::string clockLabel = "Event clock";
};

class DebugOverlay {
  Engine &engine;
  TouchPointer touchPointer;
  DebugSettings settings;
  bool visible = false;
  Clock frameClock;
  double elapsed = 0, frames = 0, fps = 0;
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
  std::unique_ptr<VoiceDubbingApplet> dubbingApplet;
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
  bool showOptionsWindow = false;
  bool showDubbingWindow = false;
  std::string selectedTransNodeStr;
  bool followActiveTranslation = false;
  bool scrollActiveTranslation = false, scrollActiveTranslationNode = false;
  std::string trackedTranslationNode, trackedTranslationKey;
  std::vector<std::string> availableVNFonts;
  std::vector<std::string> availableUIFonts;
  std::string uiFontDraft;
  bool uiFontDraftReady = false;
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
  void drawAppletHub();
  void drawOptionsWindow();
  void drawDubbingWindow();
  void drawMenuBar();
  void drawInterfaceTools();
  void drawAudioTools();
  void drawDubbingTools();
  void drawDisplayTools();
  void drawDialogueTools();
  void drawLogs();
  void applyUIFont(const std::string &fontPath);
  void drawForcedSubtitlesGenerator();
  std::string forcedSubsStatus;

public:
  explicit DebugOverlay(Engine &engine);
  ~DebugOverlay();
  DebugOverlay(const DebugOverlay &) = delete;
  DebugOverlay &operator=(const DebugOverlay &) = delete;

  bool handleEvent(EventRef event);
  void update();
  void renderHUD(RendererRef renderer);
  void renderMenu(RendererRef renderer);
  bool isVisible() const {
    return visible || showOptionsWindow || showTranslationWindow ||
           showDubbingWindow;
  }
  bool pausesStory() const { return isVisible() && !settings.ignorePause; }
  void setVisible(bool value);
  DebugSettings &getSettings() { return settings; }
  const DebugSettings &getSettings() const { return settings; }
  void trackActiveTranslation();
  std::string nodeClockText(bool eventClock = true, bool nodeClock = true) const;
  std::string performanceText(bool milliseconds = true) const;
  std::pair<std::string, std::string> hudText(float width) const;
  void reloadFonts(const std::string &fontPath);
};

} // namespace Kotonoha
