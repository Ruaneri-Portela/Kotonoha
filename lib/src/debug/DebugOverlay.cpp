#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/debug/VoiceDubbingApplet.hpp>

#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/ForcedSubtitleRenderer.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Appearance.hpp>
#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include "ImGuiTheme.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>
#include <unordered_set>

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Kotonoha {
namespace {
const char *nodeTypeName(NodeType type) {
  switch (type) {
  case NodeType::Dialogue:
    return "Dialogue";
  case NodeType::Choice:
    return "Choice";
  case NodeType::Action:
    return "Action";
  case NodeType::Conditional:
    return "Conditional";
  case NodeType::Timeline:
    return "Timeline";
  case NodeType::End:
    return "End";
  }
  return "Unknown";
}
} // namespace

namespace {
bool isCJKFont(const std::string &name) {
  return name.find("JP") != std::string::npos ||
         name.find("Japanese") != std::string::npos ||
         name.find("CJK") != std::string::npos ||
         name.find("Gothic") != std::string::npos ||
         name.find("Mincho") != std::string::npos ||
         name.find("NotoSans") != std::string::npos;
}

std::vector<std::string> uiFontNames() {
  std::vector<std::string> names;
  try {
    for (const auto &[name, bytes] : FileResolver::getFontMap()) {
      (void)bytes;
      names.push_back(name);
    }
  } catch (...) {
  }
  std::sort(names.begin(), names.end(),
            [](const std::string &left, const std::string &right) {
              const bool leftCJK = isCJKFont(left), rightCJK = isCJKFont(right);
              if (leftCJK != rightCJK)
                return leftCJK > rightCJK;
              return left < right;
            });
  if (std::find(names.begin(), names.end(), defaultFontName()) == names.end())
    names.insert(names.begin(), defaultFontName());
  return names;
}
} // namespace

DebugOverlay::DebugOverlay(Engine &engine) : engine(engine) {
  const auto ktrf =
      documentsDirectory(engine.GetRuntime().identify) / "story.ktnroute";
  SDL_strlcpy(ktrfPath, ktrf.string().c_str(), sizeof(ktrfPath));
  shaderPicker = std::make_unique<ImGuiTheme::ShaderFilePicker>();
  dubbingApplet = std::make_unique<VoiceDubbingApplet>(engine);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGuiTheme::loadFonts();
  ImGuiTheme::apply(1);
  if (!ImGui_ImplSDL3_InitForSDLRenderer(&engine.GetWindow(),
                                         &engine.GetRenderer())) {
    ImGui::DestroyContext();
    throw std::runtime_error("Cannot initialize ImGui SDL3 debug backend");
  }
  // system cursors (SDL_CreateSystemCursor) are not supported by every video
  // driver (e.g. the dummy driver used in headless tests).  The missing cursor
  // is not critical for the debug UI, but it pollutes SDL_GetError(); clear it
  // so later error reporting is not masked by this benign initialization
  // warning.
  SDL_ClearError();
  // system cursors (SDL_CreateSystemCursor) are not supported by every video
  // driver (e.g. the dummy driver used in headless tests).  The missing cursor
  // is not critical for the debug UI, but it pollutes SDL_GetError(); clear it
  // so later error reporting is not masked by this benign initialization
  // warning.
  SDL_ClearError();
  if (!ImGui_ImplSDLRenderer3_Init(&engine.GetRenderer())) {
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    throw std::runtime_error("Cannot initialize ImGui debug renderer");
  }
}

DebugOverlay::~DebugOverlay() {
  stopPreviewAudio();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void DebugOverlay::reloadFonts(const std::string &fontPath) {
  ImGui::SetCurrentContext(ImGui::GetCurrentContext());
  ImGuiTheme::loadFonts(fontPath);
}

bool DebugOverlay::handleEvent(EventRef event) {
  ImGui_ImplSDL3_ProcessEvent(&event);
  SDL_Event pointer{};
  if (touchPointer.translate(event, engine.GetWindow(), pointer)) {
    SDL_Event position{};
    position.type = SDL_EVENT_MOUSE_MOTION;
    position.motion.windowID = pointer.type == SDL_EVENT_MOUSE_MOTION
                                   ? pointer.motion.windowID
                                   : pointer.button.windowID;
    position.motion.which = SDL_TOUCH_MOUSEID;
    position.motion.x = pointer.type == SDL_EVENT_MOUSE_MOTION
                            ? pointer.motion.x
                            : pointer.button.x;
    position.motion.y = pointer.type == SDL_EVENT_MOUSE_MOTION
                            ? pointer.motion.y
                            : pointer.button.y;
    ImGui_ImplSDL3_ProcessEvent(&position);
    ImGui_ImplSDL3_ProcessEvent(&pointer);
  }
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_F10) {
      setVisible(!isVisible());
      return true;
    }
    if (event.key.key == SDLK_F9) {
      settings.showFPS = !settings.showFPS;
      return true;
    }
    if (event.key.key == SDLK_F8) {
      settings.showNodeClock = !settings.showNodeClock;
      return true;
    }
    if (event.key.key == SDLK_F7) {
      showTranslationWindow = !showTranslationWindow;
      if (showTranslationWindow)
        transSourcesStale = true;
      return true;
    }
    if (isVisible() && event.key.key == SDLK_ESCAPE) {
      setVisible(false);
      return true;
    }
  }
  const bool wantCapture = ImGui::GetIO().WantCaptureMouse ||
                           ImGui::GetIO().WantCaptureKeyboard ||
                           ImGui::IsAnyItemActive() ||
                           ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
  const bool input = (event.type >= SDL_EVENT_KEY_DOWN &&
                      event.type <= SDL_EVENT_TEXT_INPUT) ||
                     (event.type >= SDL_EVENT_MOUSE_MOTION &&
                      event.type <= SDL_EVENT_MOUSE_WHEEL) ||
                     (event.type >= SDL_EVENT_FINGER_DOWN &&
                      event.type <= SDL_EVENT_FINGER_CANCELED) ||
                     event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                     event.type == SDL_EVENT_GAMEPAD_BUTTON_UP;
  return isVisible() && input && wantCapture;
}

void DebugOverlay::update() {
  const double delta = frameClock.seconds();
  frameClock.reset();
  elapsed += delta;
  ++frames;
  if (elapsed >= 0.5) {
    fps = frames / elapsed;
    elapsed = frames = 0;
  }
}

std::string DebugOverlay::nodeClockText(bool eventClock, bool nodeClock) const {
  const auto node = engine.activeNode();
  if (node.empty())
    return settings.nodeLabel + ": --";
  std::ostringstream text;
  text << settings.nodeLabel << ": " << node << std::fixed
       << std::setprecision(2);
  if (eventClock)
    text << " | " << settings.clockLabel << ": " << engine.activeEventSeconds()
         << "s";
  if (nodeClock)
    text << " | " << settings.nodeLabel
         << " clock: " << engine.activeNodeSeconds() << "s";
  if (engine.isStoryPaused() ||
      (engine.GetRuntime().debugNode().empty() && engine.usesTimedNodeClock() &&
       engine.GetTimeline().isPaused()))
    text << " [paused]";
  return text.str();
}

std::string DebugOverlay::performanceText(bool milliseconds) const {
  std::ostringstream text;
  text << settings.fpsLabel << ": " << static_cast<int>(fps);
  if (milliseconds)
    text << " | " << std::fixed << std::setprecision(2)
         << (fps > 0 ? 1000 / fps : 0) << " ms";
  return text.str();
}

std::pair<std::string, std::string> DebugOverlay::hudText(float width) const {
  const bool showNode = settings.showNodeClock &&
                        engine.GetRuntime().GetState() == GameState::Gameplay &&
                        !engine.activeNode().empty();
  std::pair<std::string, std::string> text{settings.showFPS ? performanceText()
                                                            : "",
                                           showNode ? nodeClockText() : ""};
  const auto fits = [&] {
    const auto &ui = engine.GetUI();
    const float gap = !text.first.empty() && !text.second.empty() ? 10 : 0;
    return ui.textSize(text.first, 65535).x +
               ui.textSize(text.second, 65535).x + gap <=
           width;
  };
  if (!fits() && showNode)
    text.second = nodeClockText(true, false);
  if (!fits() && showNode)
    text.second = nodeClockText(false, false);
  if (!fits() && settings.showFPS)
    text.first = performanceText(false);
  return text;
}

void DebugOverlay::renderHUD(RendererRef renderer) {
  auto &ui = engine.GetUI();
  const auto size = UI::viewSize(renderer);
  const float width = std::max(1.0f, size.x - 20);
  const auto [performance, node] = hudText(width);
  const auto performanceSize = ui.textSize(performance, width);
  if (!performance.empty())
    ui.text(renderer, performance, 10, 10, width, {100, 200, 255, 255});
  if (!node.empty()) {
    const bool stacked =
        !performance.empty() &&
        performanceSize.x + ui.textSize(node, 65535).x + 10 > width;
    const float nodeWidth =
        stacked ? width
                : std::max(1.0f, width - performanceSize.x -
                                     (performance.empty() ? 0 : 10));
    const auto measured = ui.textSize(node, nodeWidth);
    const float x = std::max(10.0f, size.x - measured.x - 10);
    const float y = stacked ? 10 + performanceSize.y : 10;
    ui.text(renderer, node, x, y, nodeWidth, {255, 215, 100, 255});
  }
}

void DebugOverlay::drawInspector() {
  ImGuiTheme::sectionHeader("KTRF / custom nodes");
  drawKtrfTools();
  drawForcedSubtitlesGenerator();
  if (nodesStale)
    refreshNodes();
  if (ImGui::SmallButton("Refresh node list"))
    refreshNodes();
  ImGui::SameLine();
  if (ImGui::Button("Restart current scene")) {
    try {
      engine.debugRestartScene();
      status = "Restarted current scene";
      statusError = false;
      nodesStale = true;
    } catch (const std::exception &error) {
      status = error.what();
      statusError = true;
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Debug scene restart failed: %s", error.what());
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Next dialogue / event")) {
    try {
      engine.debugAdvanceEvent();
      status = "Advanced to the next dialogue/event";
      statusError = false;
    } catch (const std::exception &error) {
      status = error.what();
      statusError = true;
    }
  }
  for (const auto &[label, seconds] :
       {std::pair{"+0.1s", 0.1}, std::pair{"+1s", 1.0},
        std::pair{"+5s", 5.0}}) {
    ImGui::SameLine();
    if (ImGui::Button(label)) {
      try {
        engine.debugAdvanceScene(seconds);
        status = "Advanced scene by " + std::to_string(seconds) + "s";
        statusError = false;
      } catch (const std::exception &error) {
        status = error.what();
        statusError = true;
      }
    }
  }

  ImGuiTheme::sectionHeader("Script nodes");
  const auto &custom = cachedRuntimeNodes;
  if (!custom.empty()) {
    drawRuntimeNodes(custom);
    drawTranslationExport();
    return;
  }
  auto &manager = GetVNManagerRuntime(engine);
  const auto ids = manager.getNodeIds();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::accent());
  ImGui::TextWrapped("%s", nodeClockText().c_str());
  ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::muted());
  ImGui::TextWrapped("Debug pauses the story. Jumping executes the destination "
                     "node, but does not replay earlier inventory/actions.");
  ImGui::PopStyleColor();
  ImGui::SeparatorText("Script nodes");
  if (ids.empty()) {
    ImGui::TextUnformatted("No engine script nodes loaded.");
    drawTranslationExport();
    return;
  }
  if (std::find(ids.begin(), ids.end(), selectedNode) == ids.end())
    selectedNode = manager.getCurrentNodeId() >= 0 ? manager.getCurrentNodeId()
                                                   : ids.front();
  ImGui::BeginChild("Loaded nodes", ImVec2(0, 210), ImGuiChildFlags_Borders);
  for (int id : ids) {
    const auto &node = manager.getScriptNode(id);
    const auto label = std::to_string(id) + " | " + nodeTypeName(node.type) +
                       (node.name.empty() ? "" : " | " + node.name);
    if (ImGui::Selectable(label.c_str(), selectedNode == id))
      selectedNode = id;
  }
  ImGui::EndChild();
  const auto &node = manager.getScriptNode(selectedNode);
  ImGui::TextColored(ImGuiTheme::accent(), "Node %d | %s | next: %d", node.id,
                     nodeTypeName(node.type), node.nextNodeId);
  if (!node.speaker.empty())
    ImGui::TextWrapped("Speaker: %s", node.speaker.c_str());
  if (!node.text.empty())
    ImGui::TextWrapped("%s", node.text.c_str());
  if (!node.choiceQuestion.empty())
    ImGui::TextWrapped("%s", node.choiceQuestion.c_str());
  for (const auto &option : node.choices)
    ImGui::BulletText("%s -> %d", option.label.c_str(), option.targetNodeId);
  if (node.type == NodeType::Timeline) {
    ImGui::Text("Duration: %.2fs | %zu cues", node.timeline.duration,
                node.timeline.cues.size());
    for (const auto &cue : node.timeline.cues) {
      ImGui::BulletText("%.2f - %.2f | %s", cue.at, cue.at + cue.duration,
                        cue.value.c_str());
      if (cue.type == CueType::Audio || cue.type == CueType::Video)
        ImGui::Text("Audio channel: %s",
                    cue.channelName.empty()
                        ? (cue.type == CueType::Audio ? "audio" : "video")
                        : cue.channelName.c_str());
      if (cue.type == CueType::Choice) {
        ImGui::Indent();
        const char *policy = cue.timeout == ChoiceTimeout::Pause ? "pause"
                             : cue.timeout == ChoiceTimeout::Continue
                                 ? "continue"
                                 : "select";
        ImGui::Text("Timeout: %s | default target: %d", policy,
                    cue.timeoutTargetNodeId);
        for (const auto &option : cue.choices)
          ImGui::BulletText("%s -> %d", option.label.c_str(),
                            option.targetNodeId);
        ImGui::Unindent();
      }
    }
  }
  if (ImGuiTheme::primaryButton("Jump to selected node")) {
    try {
      manager.debugJumpToNode(selectedNode);
      status = "Jumped to node " + std::to_string(selectedNode);
      statusError = false;
    } catch (const std::exception &error) {
      status = error.what();
      statusError = true;
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Debug jump failed: %s",
                   error.what());
    }
  }
  const auto &nodes = cachedEngineNodes;
  const auto item =
      std::find_if(nodes.begin(), nodes.end(), [&](const auto &entry) {
        return entry.id == std::to_string(selectedNode);
      });
  drawAssets(item == nodes.end() ? std::vector<std::string>{} : item->assets);
  drawTranslationExport();
}

void DebugOverlay::refreshNodes() {
  nodesStale = false;
  try {
    cachedRuntimeNodes = engine.GetRuntime().debugNodes();
    cachedEngineNodes = engine.debugNodes();
  } catch (const std::exception &error) {
    cachedRuntimeNodes.clear();
    cachedEngineNodes.clear();
    status = error.what();
    statusError = true;
  }
}

void DebugOverlay::setVisible(bool value) {
  if (value && !visible)
    nodesStale = true;
  visible = value;
  if (!value) {
    showOptionsWindow = false;
    showTranslationWindow = false;
    showDubbingWindow = false;
    dubbingApplet->suspend();
    stopPreviewAudio();
  }
}

void DebugOverlay::stopPreviewAudio() {
  if (previewChannel && previewAudio)
    previewChannel->detach(previewAudio.get());
  previewAudio.reset();
  previewChannel.reset();
}

void DebugOverlay::drawForcedSubtitlesGenerator() {
  const auto &source = engine.GetForcedSubtitles().sourceDirectory();
  ImGui::TextWrapped("Forced ASS directory: %s", source.empty()
                                                     ? "(not found at startup)"
                                                     : source.string().c_str());
  const auto &activeAss = engine.GetForcedSubtitles().activePath();
  ImGui::TextWrapped("Active forced ASS: %s",
                     activeAss.empty() ? "(none)" : activeAss.string().c_str());
  if (!ImGui::CollapsingHeader("Generate Forced Subtitles (.ass)"))
    return;

  if (engine.activeNode().empty()) {
    ImGui::TextDisabled("No active node/event.");
    return;
  }

  ImGui::TextWrapped("%s", nodeClockText().c_str());
  ImGui::TextWrapped("ASS times use the event clock for a full node/event "
                     "file, or the node clock for a node-only fallback. A "
                     "full node/event file takes precedence; existing files "
                     "are never overwritten.");

  if (ImGui::Button("Generate ASS Scaffolding File")) {
    try {
      forcedSubsStatus =
          "Generated: " +
          engine.GetForcedSubtitles().dumpForActiveNode().string();
    } catch (const std::exception &error) {
      forcedSubsStatus = std::string("Error: ") + error.what();
      SDL_LogError(LogMedia, "Forced subtitle dump: %s", error.what());
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Reload forced subtitles")) {
    engine.GetForcedSubtitles().reload();
    forcedSubsStatus = "Forced subtitles will reload on the next frame.";
  }

  auto &manager = GetVNManagerRuntime(engine);
  const int id = manager.getCurrentNodeId();
  if (engine.GetRuntime().debugNode().empty() && id >= 0 &&
      !manager.isFinished()) {
    const auto &node = manager.getScriptNode(id);
    if (node.type == NodeType::Timeline) {
      ImGui::SameLine();
      if (ImGui::Button("Jump to cue..."))
        ImGui::OpenPopup("SelectCue");
      if (ImGui::BeginPopupModal("SelectCue", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Jump to video cue:");
        for (size_t i = 0; i < node.timeline.cues.size(); ++i) {
          const auto &cue = node.timeline.cues[i];
          if (cue.type != CueType::Video)
            continue;
          const auto label =
              "Cue " + std::to_string(i) + " @ " + std::to_string(cue.at) + "s";
          if (ImGui::Button(label.c_str())) {
            try {
              engine.GetTimeline().seek(cue.at);
              forcedSubsStatus = "Jumped to " + std::to_string(cue.at) + "s";
            } catch (const std::exception &error) {
              forcedSubsStatus = std::string("Error: ") + error.what();
              SDL_LogError(LogMedia, "Timeline seek: %s", error.what());
            }
            ImGui::CloseCurrentPopup();
          }
        }
        if (ImGui::Button("Cancel"))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
    }
  }

  if (!forcedSubsStatus.empty()) {
    ImGui::TextColored(ImGuiTheme::accent(), "%s", forcedSubsStatus.c_str());
  }
}

void DebugOverlay::drawKtrfTools() {
  if (!ImGui::CollapsingHeader("KTRF / custom nodes"))
    return;
  ImGui::InputText("KTRF path", ktrfPath, sizeof(ktrfPath));
  ImGui::TextWrapped("Dump the current graph, edit a .json dump or supply a "
                     "compatible .ktnroute, then reload it. Loading replaces "
                     "the graph; native callbacks must already be registered.");
  auto operation = [&](bool load) {
    try {
      if (load)
        engine.loadKtrf(ktrfPath);
      else
        engine.dumpKtrf(ktrfPath);
      status = std::string(load ? "Loaded KTRF: " : "Dumped KTRF: ") + ktrfPath;
      statusError = false;
      selectedRuntimeNode.clear();
      nodesStale = true;
      previewTexture.reset();
      stopPreviewAudio();
    } catch (const std::exception &error) {
      status = error.what();
      statusError = true;
      SDL_LogError(LogCore, "KTRF: %s", error.what());
    }
  };
  if (ImGui::Button("Dump current KTRF"))
    operation(false);
  ImGui::SameLine();
  if (ImGui::Button("Load / replace KTRF"))
    operation(true);
}

void DebugOverlay::drawRuntimeNodes(const std::vector<DebugNode> &nodes) {
  ImGui::TextWrapped("%s", nodeClockText().c_str());
  ImGui::TextWrapped(
      "Jumping keeps current variables; earlier events are not replayed. "
      "Replay-based saves are disabled after a jump until a normal "
      "restart/load.");
  if (std::none_of(nodes.begin(), nodes.end(), [&](const auto &node) {
        return node.id == selectedRuntimeNode;
      }))
    selectedRuntimeNode = nodes.front().id;
  ImGui::BeginChild("Runtime nodes", ImVec2(0, 210), ImGuiChildFlags_Borders);
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(nodes.size()));
  while (clipper.Step())
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const auto &node = nodes[static_cast<std::size_t>(i)];
      const auto label = node.id + " | " + node.label;
      if (ImGui::Selectable(label.c_str(), node.id == selectedRuntimeNode))
        selectedRuntimeNode = node.id;
    }
  ImGui::EndChild();
  if (ImGuiTheme::primaryButton("Jump to selected node")) {
    try {
      engine.debugJumpToNode(selectedRuntimeNode);
      status = "Jumped to " + selectedRuntimeNode;
      statusError = false;
      stopPreviewAudio();
    } catch (const std::exception &error) {
      status = error.what();
      statusError = true;
      SDL_LogError(LogCore, "Node jump: %s", error.what());
    }
  }
  const auto node =
      std::find_if(nodes.begin(), nodes.end(), [&](const auto &item) {
        return item.id == selectedRuntimeNode;
      });
  drawAssets(node->assets);
}

void DebugOverlay::drawAssets(const std::vector<std::string> &nodeAssets) {
  if (!ImGui::CollapsingHeader("Scene / node assets"))
    return;
  auto choose = [&](const std::string &id) {
    SDL_strlcpy(assetPath, id.c_str(), sizeof(assetPath));
    previewTexture.reset();
    previewAsset.clear();
    previewError.clear();
    previewSeconds = 0;
    stopPreviewAudio();
  };
  ImGui::Text("Provider: %s", engine.GetAssets().name().c_str());
  for (const auto &id : nodeAssets)
    if (ImGui::Selectable(id.c_str(), id == assetPath))
      choose(id);
  if (nodeAssets.empty())
    ImGui::TextDisabled(
        "No asset references on this node. Enter an asset ID below.");
  if (ImGui::Button("List provider assets")) {
    try {
      browsedAssets = engine.GetAssets().list();
      if (browsedAssets.empty())
        previewError =
            "This provider has no enumerable assets; enter an asset ID.";
    } catch (const std::exception &error) {
      previewError = error.what();
      SDL_LogError(LogCore, "Asset enumeration: %s", error.what());
    }
  }
  if (!browsedAssets.empty()) {
    ImGui::BeginChild("Provider assets", ImVec2(0, 150),
                      ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(browsedAssets.size()));
    while (clipper.Step())
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        if (ImGui::Selectable(browsedAssets[i].c_str(),
                              browsedAssets[i] == assetPath))
          choose(browsedAssets[i]);
    ImGui::EndChild();
  }
  if (ImGui::InputText("Asset ID", assetPath, sizeof(assetPath)))
    previewError.clear();
  ImGui::InputFloat("Frame position (seconds)", &previewSeconds, 1, 10, "%.2f");
  if (ImGui::Button("Preview picture / video frame")) {
    try {
      if (!std::isfinite(previewSeconds) || previewSeconds < 0)
        throw std::invalid_argument(
            "Frame position must be finite and nonnegative");
      previewTexture.reset();
      auto surface = engine.GetAssets().loadFrame(assetPath, previewSeconds);
      previewTexture.reset(
          SDL_CreateTextureFromSurface(&engine.GetRenderer(), surface.get()));
      if (!previewTexture)
        throw std::runtime_error(SDL_GetError());
      previewAsset = assetPath;
      previewError.clear();
    } catch (const std::exception &error) {
      previewError = error.what();
      SDL_LogError(LogMedia, "Asset preview: %s", error.what());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Play audio")) {
    try {
      stopPreviewAudio();
      const auto clip = engine.GetAssets().loadAudio(assetPath);
      if (clip.data.empty() ||
          clip.data.size() >
              static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Audio preview is empty or too large");
      previewAudio.reset(SDL_OpenAudioDeviceStream(
          SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &clip.spec, nullptr, nullptr));
      if (!previewAudio)
        throw std::runtime_error(SDL_GetError());
      previewChannel = engine.GetAudio().channel("debug-preview");
      previewChannel->attach(previewAudio.get());
      if (!SDL_PutAudioStreamData(previewAudio.get(), clip.data.data(),
                                  static_cast<int>(clip.data.size())) ||
          !SDL_FlushAudioStream(previewAudio.get()) ||
          !SDL_ResumeAudioStreamDevice(previewAudio.get()))
        throw std::runtime_error(SDL_GetError());
      previewError.clear();
    } catch (const std::exception &error) {
      stopPreviewAudio();
      previewError = error.what();
      SDL_LogError(LogMedia, "Audio preview: %s", error.what());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop audio"))
    stopPreviewAudio();
  if (previewTexture) {
    float width = 0, height = 0;
    if (SDL_GetTextureSize(previewTexture.get(), &width, &height) &&
        width > 0 && height > 0) {
      const float scale = std::min(
          {1.0f, ImGui::GetContentRegionAvail().x / width, 320.0f / height});
      ImGui::TextWrapped("%s (%.0f x %.0f)", previewAsset.c_str(), width,
                         height);
      ImGui::Image(ImTextureRef(previewTexture.get()),
                   ImVec2(width * scale, height * scale));
    }
  }
  if (!previewError.empty())
    ImGuiTheme::statusText(previewError.c_str(), true);
}

void DebugOverlay::refreshTranslationSources() {
  transSourcesStale = false;
  // Garante que os nodos de runtime (ex: School Days KTRF) estejam carregados
  // antes de listar as chaves de tradução.
  if (nodesStale)
    refreshNodes();
  try {
    cachedManagerSources = GetVNManagerRuntime(engine).translationSources();
    cachedRuntimeSources = engine.GetRuntime().translationSources();

    cachedAllNodeKeys.clear();
    std::unordered_set<std::string> uniqueKeys;
    const auto addKey = [&](const std::string &key) {
      if (key.empty())
        return;
      if (uniqueKeys.insert(key).second)
        cachedAllNodeKeys.push_back(key);
    };
    if (auto current = engine.GetRuntime().translationRoute(); current >= 0)
      addKey(std::to_string(current));
    addKey(engine.GetRuntime().debugNode());

    for (int id : GetVNManagerRuntime(engine).getNodeIds())
      addKey(std::to_string(id));
    for (const auto &[id, _] : cachedManagerSources)
      addKey(std::to_string(id));
    for (int id : engine.GetRuntime().translationRoutes())
      addKey(id < 0 ? "nodo" : std::to_string(id));
    for (const auto &[id, _] : cachedRuntimeSources)
      addKey(std::to_string(id));
    for (const auto &engNode : cachedEngineNodes)
      addKey(engNode.id);
    for (const auto &rtNode : cachedRuntimeNodes)
      addKey(rtNode.id);
    for (const auto &[secName, _] : engine.GetTranslation().getSections())
      addKey(secName);

    std::sort(cachedAllNodeKeys.begin(), cachedAllNodeKeys.end(),
              [](const std::string &a, const std::string &b) {
                int idA = -1, idB = -1;
                auto pA = std::from_chars(a.data(), a.data() + a.size(), idA);
                auto pB = std::from_chars(b.data(), b.data() + b.size(), idB);
                const bool numericA =
                    pA.ec == std::errc{} && pA.ptr == a.data() + a.size();
                const bool numericB =
                    pB.ec == std::errc{} && pB.ptr == b.data() + b.size();
                if (numericA != numericB)
                  return numericA;
                if (numericA)
                  return idA < idB;
                return a < b;
              });
  } catch (...) {
  }
}

void DebugOverlay::trackActiveTranslation() {
  showTranslationWindow = true;
  followActiveTranslation = true;
  trackedTranslationNode.clear();
  trackedTranslationKey.clear();
  scrollActiveTranslation = scrollActiveTranslationNode = true;
  transSearchFilter[0] = 0;
}
void DebugOverlay::drawTranslationWindow() {
  if (!showTranslationWindow)
    return;

  if (transSourcesStale)
    refreshTranslationSources();

  auto &io = ImGui::GetIO();
  const float displayScale = SDL_GetWindowDisplayScale(&engine.GetWindow());
  const float pixelDensity = SDL_GetWindowPixelDensity(&engine.GetWindow());
  const float scale =
      (displayScale > 0 && pixelDensity > 0)
          ? displayScale / pixelDensity * engine.GetSettings().debugUIScale
          : 1.0f;

  ImGui::SetNextWindowSize(ImVec2(std::min(760.0f * scale, io.DisplaySize.x),
                                  std::min(600.0f * scale, io.DisplaySize.y)),
                           ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Translation Applet [F7]", &showTranslationWindow,
                    ImGuiWindowFlags_MenuBar)) {
    ImGui::End();
    return;
  }
  drawMenuBar();

  auto &translation = engine.GetTranslation();
  auto &sections = translation.mutableSections();
  auto &manager = GetVNManagerRuntime(engine);

  std::string activeNodeKey;
  const int mgrActiveId = manager.getCurrentNodeId();
  if (mgrActiveId >= 0) {
    activeNodeKey = std::to_string(mgrActiveId);
  } else if (engine.GetRuntime().translationRoute() >= 0) {
    activeNodeKey = std::to_string(engine.GetRuntime().translationRoute());
  } else {
    std::string rtDebug = engine.GetRuntime().debugNode();
    if (!rtDebug.empty()) {
      auto pos = rtDebug.find("scene ");
      if (pos != std::string::npos) {
        int scIdx = -1;
        std::from_chars(rtDebug.data() + pos + 6,
                        rtDebug.data() + rtDebug.size(), scIdx);
        if (scIdx >= 0)
          activeNodeKey = std::to_string(scIdx);
      }
      if (activeNodeKey.empty())
        activeNodeKey = rtDebug;
    }
  }

  auto activeKeys = engine.GetRuntime().activeTranslationKeys();
  if (activeKeys.empty() && mgrActiveId >= 0) {
    try {
      const auto &node = manager.getScriptNode(mgrActiveId);
      if (!node.text.empty())
        activeKeys.push_back(node.text);
      if (!node.choiceQuestion.empty())
        activeKeys.push_back(node.choiceQuestion);
      for (const auto &choice : node.choices)
        activeKeys.push_back(choice.label);
    } catch (const std::exception &) {
    }
  }
  if (activeKeys.empty())
    for (auto *scene : engine.GetRuntime().GetActiveScenes()) {
      if (auto *box = dynamic_cast<VNTextBox *>(scene);
          box && box->isVisible() && !box->getText().empty())
        activeKeys.push_back(box->getText());
      if (auto *choice = dynamic_cast<ChoiceQuestionScene *>(scene);
          choice && choice->isActive()) {
        if (!choice->getQuestion().empty())
          activeKeys.push_back(choice->getQuestion());
        for (const auto &option : choice->getOptions())
          activeKeys.push_back(option.label);
      }
    }
  const auto currentKey =
      activeKeys.empty() ? std::string() : activeKeys.front();
  if (followActiveTranslation && !activeNodeKey.empty() &&
      (trackedTranslationNode != activeNodeKey ||
       trackedTranslationKey != currentKey ||
       selectedTransNodeStr != activeNodeKey)) {
    selectedTransNodeStr = activeNodeKey;
    scrollActiveTranslationNode = trackedTranslationNode != activeNodeKey;
    scrollActiveTranslation = true;
    trackedTranslationNode = activeNodeKey;
    trackedTranslationKey = currentKey;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::accent());
  ImGui::Text("Active Node: %s",
              !activeNodeKey.empty() ? activeNodeKey.c_str() : "None / --");
  ImGui::PopStyleColor();

  ImGui::SameLine();
  if (ImGui::Button("Track Active Node") && !activeNodeKey.empty()) {
    trackActiveTranslation();
    selectedTransNodeStr = activeNodeKey;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Follow active string", &followActiveTranslation);

  ImGui::SameLine();
  if (ImGui::Button("Refresh Node List")) {
    refreshTranslationSources();
  }

  if (selectedTransNodeStr.empty() && !cachedAllNodeKeys.empty()) {
    selectedTransNodeStr =
        !activeNodeKey.empty() ? activeNodeKey : cachedAllNodeKeys.front();
  }

  ImGuiTheme::sectionHeader("Script Nodes");
  ImGui::InputText("Buscar Nodo/Chave", transSearchFilter,
                   sizeof(transSearchFilter));

  std::string filterStr = transSearchFilter;
  std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(),
                 ::tolower);

  ImGui::BeginChild("TransNodeList", ImVec2(0, 160), ImGuiChildFlags_Borders);
  for (const auto &nodeKey : cachedAllNodeKeys) {
    if (!filterStr.empty()) {
      std::string lowerKey = nodeKey;
      std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
                     ::tolower);
      if (lowerKey.find(filterStr) == std::string::npos)
        continue;
    }

    bool isCurrent = (nodeKey == activeNodeKey);
    std::string label = "Node " + nodeKey;
    if (isCurrent)
      label += " [CURRENT ACTIVE]";

    if (isCurrent)
      ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::accent());

    bool selected = (selectedTransNodeStr == nodeKey);
    if (ImGui::Selectable(label.c_str(), selected)) {
      selectedTransNodeStr = nodeKey;
      followActiveTranslation = false;
    }
    if (isCurrent && selected && scrollActiveTranslationNode) {
      ImGui::SetScrollHereY(0.5f);
      scrollActiveTranslationNode = false;
    }

    if (isCurrent) {
      ImGui::PopStyleColor();
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
  }
  ImGui::EndChild();

  if (!selectedTransNodeStr.empty()) {
    ImGuiTheme::sectionHeader(
        ("Node " + selectedTransNodeStr + " Scene Strings").c_str());

    std::vector<std::string> stringsToTranslate;
    const auto addString = [&](const std::string &s) {
      if (!s.empty() &&
          std::find(stringsToTranslate.begin(), stringsToTranslate.end(), s) ==
              stringsToTranslate.end())
        stringsToTranslate.push_back(s);
    };

    if (selectedTransNodeStr == "nodo")
      for (const auto &text : engine.GetRuntime().translationTexts(-1))
        addString(text);
    int nodeInt = -1;
    auto parsed = std::from_chars(
        selectedTransNodeStr.data(),
        selectedTransNodeStr.data() + selectedTransNodeStr.size(), nodeInt);
    if (parsed.ec == std::errc{} && nodeInt >= 0) {
      try {
        const auto &node = manager.getScriptNode(nodeInt);
        if (!node.speaker.empty() && node.speaker != "Narration")
          addString(node.speaker);
        if (!node.text.empty())
          addString(node.text);
        if (!node.choiceQuestion.empty())
          addString(node.choiceQuestion);
        for (const auto &opt : node.choices)
          addString(opt.label);
      } catch (...) {
      }

      for (const auto &engNode : cachedEngineNodes) {
        if (engNode.id == std::to_string(nodeInt) ||
            engNode.id == selectedTransNodeStr) {
          addString(engNode.label);
        }
      }
      for (const auto &rtNode : cachedRuntimeNodes) {
        if (rtNode.id == std::to_string(nodeInt) ||
            rtNode.id == selectedTransNodeStr) {
          addString(rtNode.label);
        }
      }

      auto mIt = cachedManagerSources.find(nodeInt);
      if (mIt != cachedManagerSources.end()) {
        for (const auto &s : mIt->second)
          addString(s);
      }

      if (cachedRuntimeSources.find(nodeInt) == cachedRuntimeSources.end())
        cachedRuntimeSources.emplace(
            nodeInt, engine.GetRuntime().translationTexts(nodeInt));
      auto rIt = cachedRuntimeSources.find(nodeInt);
      if (rIt != cachedRuntimeSources.end()) {
        for (const auto &s : rIt->second)
          addString(s);
      }
    } else {
      // Chave não numérica (ex: School Days scene key
      // "sdhq:node:00/00-00-A05"). Mostra as legendas da cena atual quando a
      // chave corresponde ao nodo ativo.
      if (selectedTransNodeStr == engine.GetRuntime().debugNode()) {
        auto texts = engine.GetRuntime().translationTexts(0);
        for (const auto &s : texts)
          addString(s);
      }
      // Também expõe rótulos dos nodos de runtime que correspondam à chave.
      for (const auto &rtNode : cachedRuntimeNodes) {
        if (rtNode.id == selectedTransNodeStr ||
            rtNode.label == selectedTransNodeStr)
          addString(rtNode.label);
      }
      for (const auto &engNode : cachedEngineNodes) {
        if (engNode.id == selectedTransNodeStr ||
            engNode.label == selectedTransNodeStr)
          addString(engNode.label);
      }
    }

    // Always include the currently displayed subtitle/scene strings so live
    // translation can find and update them even for runtimes without a VN
    // manager (e.g. School Days).
    for (const auto &key : activeKeys)
      addString(key);

    if (stringsToTranslate.empty() &&
        nodeInt == engine.GetRuntime().translationRoute()) {
      for (auto *scene : engine.GetRuntime().GetActiveScenes()) {
        if (auto *box = dynamic_cast<VNTextBox *>(scene);
            box && box->isVisible()) {
          addString(box->getSpeaker());
          addString(box->getText());
        }
        if (auto *choice = dynamic_cast<ChoiceQuestionScene *>(scene);
            choice && choice->isActive()) {
          addString(choice->getQuestion());
          for (const auto &option : choice->getOptions())
            addString(option.label);
        }
      }
    }
    auto secIt = sections.find(selectedTransNodeStr);
    if (secIt != sections.end()) {
      for (const auto &[orig, _] : secIt->second)
        addString(orig);
    }

    if (stringsToTranslate.empty()) {
      ImGui::TextDisabled("No scene strings found for Node %s",
                          selectedTransNodeStr.c_str());
    } else {
      ImGui::BeginChild("StringEntries", ImVec2(0, 0), true);
      int idx = 0;
      for (const auto &orig : stringsToTranslate) {
        ImGui::PushID(idx++);
        const bool current =
            selectedTransNodeStr == activeNodeKey &&
            std::any_of(
                activeKeys.begin(), activeKeys.end(),
                [&](const std::string &key) {
                  if (orig == key || translation.text(nodeInt, orig) == key)
                    return true;
                  const auto section = sections.find(selectedTransNodeStr);
                  if (section == sections.end())
                    return false;
                  const auto translated = section->second.find(orig);
                  return translated != section->second.end() &&
                         translated->second == key;
                });
        ImGui::BeginGroup();
        if (current) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::accent());
          ImGui::TextUnformatted("CURRENT LINE");
        }
        ImGui::TextWrapped("Chave (Original): %s", orig.c_str());
        if (current)
          ImGui::PopStyleColor();

        if (ImGui::Button("Copiar Chave")) {
          SDL_SetClipboardText(orig.c_str());
        }
        ImGui::SameLine();

        std::string currentVal =
            translation.text(nodeInt >= 0 ? nodeInt : -1, orig);
        const auto currentSection = sections.find(selectedTransNodeStr);
        if (currentVal == orig && currentSection != sections.end()) {
          auto foundVal = currentSection->second.find(orig);
          if (foundVal != currentSection->second.end())
            currentVal = foundVal->second;
        }

        std::string inputBuffer = currentVal;

        if (ImGui::Button("Colar")) {
          SDLMemoryPtr<char> clip(SDL_GetClipboardText());
          if (clip) {
            inputBuffer = clip.get();
            engine.setTranslation(selectedTransNodeStr, orig, inputBuffer);
          }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Tradução:");

        if (ImGui::InputTextMultiline("##transInput", &inputBuffer,
                                      ImVec2(-1, 54))) {
          engine.setTranslation(selectedTransNodeStr, orig, inputBuffer);
        }

        ImGui::EndGroup();
        if (current) {
          const auto min = ImGui::GetItemRectMin(),
                     max = ImGui::GetItemRectMax();
          ImGui::GetWindowDrawList()->AddRect(
              min, max, ImGui::GetColorU32(ImGuiTheme::accent()), 4.0f, 2.0f,
              ImDrawFlags_None);
          if (scrollActiveTranslation) {
            ImGui::SetScrollHereY(0.25f);
            scrollActiveTranslation = false;
          }
        }
        ImGui::PopID();
        ImGui::Separator();
      }
      ImGui::EndChild();
    }
  }

  ImGui::End();
}

void DebugOverlay::drawTranslationEditor() {
  if (ImGui::Button("Open Translation Applet (F7)")) {
    showTranslationWindow = true;
  }
}

void DebugOverlay::drawTranslationExport() {
  drawTranslationEditor();
  ImGui::SeparatorText("Translation Export");
  ImGui::InputText("Export path", exportPath, sizeof(exportPath));
  ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::muted());
  ImGui::TextWrapped(
      "Exports all original texts by route (script nodes and the game's own "
      "routes); loaded translations are preserved. An existing file at this "
      "path will be replaced.");
  ImGui::PopStyleColor();
  if (ImGui::Button("Dump translation.json")) {
    try {
      engine.dumpTranslation(exportPath);
      status = std::string("Exported dictionary: ") + exportPath;
      statusError = false;
    } catch (const std::exception &error) {
      status = error.what();
      statusError = true;
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "Translation export failed: %s", error.what());
    }
  }
}

void DebugOverlay::drawMenuBar() {
  if (!ImGui::BeginMenuBar())
    return;
  if (ImGui::BeginMenu("System")) {
    if (ImGui::MenuItem("Open system menu")) {
      setVisible(false);
      engine.openSettings();
    }
    if (ImGui::MenuItem("Restart current scene")) {
      try {
        engine.debugRestartScene();
        status = "Restarted current scene";
        statusError = false;
        nodesStale = transSourcesStale = true;
      } catch (const std::exception &error) {
        status = error.what();
        statusError = true;
      }
    }
    if (ImGui::MenuItem("Close debug UI", "F10"))
      setVisible(false);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("FPS", "F9", &settings.showFPS);
    ImGui::MenuItem("Node / clock", "F8", &settings.showNodeClock);
    ImGui::MenuItem("Ignore pause", nullptr, &settings.ignorePause);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Applets")) {
    if (ImGui::MenuItem("Options", nullptr, showOptionsWindow))
      showOptionsWindow = true;
    if (ImGui::MenuItem("Translation", "F7", showTranslationWindow)) {
      showTranslationWindow = true;
      transSourcesStale = true;
    }
    if (ImGui::MenuItem("Dubbing", nullptr, showDubbingWindow))
      showDubbingWindow = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    ImGui::MenuItem("F10 toggles this debug UI", nullptr, false, false);
    ImGui::MenuItem("F7 opens the translation editor", nullptr, false, false);
    ImGui::MenuItem("Esc closes the debug UI", nullptr, false, false);
    ImGui::EndMenu();
  }
  ImGui::EndMenuBar();
}

void DebugOverlay::drawAppletHub() {
  if (!visible)
    return;
  auto &io = ImGui::GetIO();
  ImGui::SetNextWindowSize(
      ImVec2(std::min(760 * contentScale, io.DisplaySize.x),
             std::min(720 * contentScale, io.DisplaySize.y)),
      ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Kotonoha Debug Applets [F10]", &visible,
                   ImGuiWindowFlags_MenuBar)) {
    drawMenuBar();
    ImGuiTheme::sectionHeader("Applets");
    ImGui::TextWrapped("Each tool has its own window and can remain open "
                       "independently while you inspect the running game.");
    if (ImGuiTheme::primaryButton("Options", ImVec2(180 * contentScale, 42)))
      showOptionsWindow = true;
    ImGui::SameLine();
    if (ImGuiTheme::primaryButton("Translation",
                                  ImVec2(180 * contentScale, 42))) {
      showTranslationWindow = true;
      transSourcesStale = true;
    }
    ImGui::SameLine();
    if (ImGuiTheme::primaryButton("Dubbing", ImVec2(180 * contentScale, 42)))
      showDubbingWindow = true;
    ImGui::SeparatorText("Scene inspector");
    drawInspector();
    if (!status.empty())
      ImGuiTheme::statusText(status.c_str(), statusError);
  }
  ImGui::End();
}

void DebugOverlay::drawOptionsWindow() {
  if (!showOptionsWindow)
    return;
  auto &io = ImGui::GetIO();
  ImGui::SetNextWindowSize(
      ImVec2(std::min(720 * contentScale, io.DisplaySize.x),
             std::min(640 * contentScale, io.DisplaySize.y)),
      ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Options Applet", &showOptionsWindow,
                   ImGuiWindowFlags_MenuBar)) {
    drawMenuBar();
    if (ImGui::BeginTabBar("##options-tabs", ImGuiTabBarFlags_Reorderable)) {
      if (ImGui::BeginTabItem("Interface")) {
        drawInterfaceTools();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Audio")) {
        drawAudioTools();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Display")) {
        drawDisplayTools();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Dialogue")) {
        drawDialogueTools();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Logs")) {
        drawLogs();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    if (!status.empty())
      ImGuiTheme::statusText(status.c_str(), statusError);
  }
  ImGui::End();
}

void DebugOverlay::drawDubbingWindow() {
  if (!showDubbingWindow)
    return;
  auto &io = ImGui::GetIO();
  ImGui::SetNextWindowSize(
      ImVec2(std::min(760 * contentScale, io.DisplaySize.x),
             std::min(700 * contentScale, io.DisplaySize.y)),
      ImGuiCond_FirstUseEver);
  const bool wasOpen = showDubbingWindow;
  if (ImGui::Begin("Dubbing Applet", &showDubbingWindow,
                   ImGuiWindowFlags_MenuBar)) {
    drawMenuBar();
    drawDubbingTools();
  }
  ImGui::End();
  if (wasOpen && !showDubbingWindow)
    dubbingApplet->suspend();
}

void DebugOverlay::renderMenu(RendererRef renderer) {
  if (!SDL_SetRenderLogicalPresentation(&renderer, 0, 0,
                                        SDL_LOGICAL_PRESENTATION_DISABLED))
    throw std::runtime_error(SDL_GetError());
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  auto &io = ImGui::GetIO();
  int pixelWidth = 0, pixelHeight = 0;
  if (!SDL_GetCurrentRenderOutputSize(&renderer, &pixelWidth, &pixelHeight))
    throw std::runtime_error(SDL_GetError());
  // Headless/dummy video drivers report a 0x0 output size.  Fall back to the
  // ImGui display size (or a sane default) so the debug UI can still process
  // frames in headless test environments instead of bailing out early.
  if (pixelWidth <= 0 || pixelHeight <= 0) {
    if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0) {
      pixelWidth = static_cast<int>(io.DisplaySize.x);
      pixelHeight = static_cast<int>(io.DisplaySize.y);
    } else {
      pixelWidth = 1280;
      pixelHeight = 720;
    }
  }
  if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
    io.DisplaySize = {(float)pixelWidth, (float)pixelHeight};
  }
  io.DisplayFramebufferScale = {pixelWidth / io.DisplaySize.x,
                                pixelHeight / io.DisplaySize.y};
  // Retina already uses points. Scale layout only for content scaling not
  // represented by the framebuffer density (e.g. Windows display scaling).
  const float displayScale = SDL_GetWindowDisplayScale(&engine.GetWindow());
  const float pixelDensity = SDL_GetWindowPixelDensity(&engine.GetWindow());
  // Headless drivers may report 0 for display scale / pixel density.
  if (displayScale <= 0)
    throw std::runtime_error(SDL_GetError());
  if (pixelDensity <= 0)
    throw std::runtime_error(SDL_GetError());
  const float scale =
      displayScale / pixelDensity * engine.GetSettings().debugUIScale;
  if (contentScale != scale) {
    ImGuiTheme::apply(scale);
    contentScale = scale;
  }
  ImGui::NewFrame();
  drawAppletHub();
  drawOptionsWindow();
  drawTranslationWindow();
  drawDubbingWindow();
  if (!isVisible())
    stopPreviewAudio();
  ImGui::Render();
  // SDLRenderer3 expects vertex scaling through SDL, not just FramebufferScale.
  // Disable logical presentation so the density is applied exactly once.
  if (!SDL_SetRenderViewport(&renderer, nullptr) ||
      !SDL_SetRenderClipRect(&renderer, nullptr) ||
      !SDL_SetRenderScale(&renderer, io.DisplayFramebufferScale.x,
                          io.DisplayFramebufferScale.y))
    throw std::runtime_error(SDL_GetError());
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), &renderer);
}

void DebugOverlay::applyUIFont(const std::string &fontPath) {
  auto draft = engine.GetSettings();
  draft.fontPath = fontPath.empty() ? defaultFontName() : fontPath;
  try {
    engine.applySettings(draft);
    status = "UI font applied: " + draft.fontPath;
    statusError = false;
  } catch (const std::exception &error) {
    status = error.what();
    statusError = true;
    SDL_LogError(LogConfig, "UI font: %s", error.what());
  }
}

void DebugOverlay::drawInterfaceTools() {
  ImGuiTheme::cardBegin("card_interface", false, nullptr);
  {
    ImGuiTheme::sectionHeader("UI font");
    if (availableUIFonts.empty())
      availableUIFonts = uiFontNames();
    auto draft = engine.GetSettings();
    const std::string current = draft.fontPath.empty()
                                    ? std::string(defaultFontName())
                                    : draft.fontPath;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##uiFontCombo", current.c_str())) {
      for (const auto &font : availableUIFonts) {
        const auto label =
            isCJKFont(font) ? font + " [CJK / JP]" : font + " [Latin]";
        const bool selected =
            font == draft.fontPath ||
            (draft.fontPath.empty() && font == defaultFontName());
        if (ImGui::Selectable(label.c_str(), selected)) {
          applyUIFont(font);
          uiFontDraft = font;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!uiFontDraftReady) {
      uiFontDraft = current;
      uiFontDraftReady = true;
    }
    ImGui::InputText("##uiFontPath", &uiFontDraft);
    if (ImGui::Button("Apply font"))
      applyUIFont(uiFontDraft);
    ImGui::SameLine();
    if (ImGui::Button("Reset to NotoSansJP")) {
      uiFontDraft = defaultFontName();
      applyUIFont(defaultFontName());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh list"))
      availableUIFonts.clear();
    ImGui::TextWrapped(
        "Applies immediately to the SDL UI, ImGui, Debug UI and the "
        "FPS / node / clock HUD. Empty uses the engine default (%s).",
        defaultFontName());
    ImGui::SeparatorText("Debug UI scale");
    if (ImGui::SliderFloat("Scale", &draft.debugUIScale, 1, 3, "%.2fx")) {
      try {
        engine.applySettings(draft);
      } catch (const std::exception &error) {
        status = error.what();
        statusError = true;
        SDL_LogError(LogConfig, "Debug UI scale: %s", error.what());
      }
    }
  }
  ImGuiTheme::cardEnd();
}

void DebugOverlay::drawAudioTools() {
  ImGuiTheme::cardBegin("card_audio", false, nullptr);
  {
    ImGuiTheme::sectionHeader("Audio channels");
    auto draft = engine.GetSettings();
    bool changed =
        ImGui::SliderFloat("Master volume", &draft.masterVolume, 0, 1, "%.2f");
    changed = ImGui::Checkbox("Mute all audio", &draft.masterMuted) || changed;
    if (changed) {
      try {
        engine.applySettings(draft);
      } catch (const std::exception &error) {
        status = error.what();
        statusError = true;
        SDL_LogError(LogMedia, "Master volume: %s", error.what());
      }
    }
    if (ImGui::BeginTable("audio channels", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_NoSavedSettings |
                              ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch,
                              1.0f);
      ImGui::TableSetupColumn("Streams", ImGuiTableColumnFlags_WidthFixed,
                              90.0f);
      ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch,
                              2.0f);
      ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableHeadersRow();
      for (const auto &channel : engine.GetAudio().getChannels()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(channel->getName().c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiTheme::accent());
        ImGui::TextUnformatted(channel->getName().c_str());
        ImGui::PopStyleColor();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%zu streams", channel->activeStreams());
        ImGui::TableSetColumnIndex(2);
        float volume = channel->getVolume();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##volume", &volume, 0, 1, "%.2f"))
          channel->setVolume(volume);
        ImGui::TableSetColumnIndex(3);
        bool muted = channel->isMuted();
        if (ImGui::Checkbox("##mute", &muted))
          channel->setMuted(muted);
        try {
          // Re-read in case the channel mutated state externally.
          volume = channel->getVolume();
          muted = channel->isMuted();
        } catch (const std::exception &error) {
          status = error.what();
          statusError = true;
          SDL_LogError(LogMedia, "Audio channel '%s': %s",
                       channel->getName().c_str(), error.what());
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
  }
  ImGuiTheme::cardEnd();
}

void DebugOverlay::drawDubbingTools() { dubbingApplet->draw(); }

void DebugOverlay::drawDisplayTools() {
  ImGuiTheme::cardBegin("card_display", false, nullptr);
  {
    ImGuiTheme::sectionHeader("Window / renderer / upscaler");
    const auto &settings = engine.GetSettings();
    ImGui::Text("Window: %dx%d | display: %d | VSync: %s", settings.width,
                settings.height, settings.display + 1,
                settings.vsync ? "on" : "off");
    ImGui::Text("Renderer: %s", SDL_GetRendererName(&engine.GetRenderer()));
    auto &upscaling = engine.GetUpscaler();
    ImGui::Text("Upscaler: %s -> %s",
                UpscaleBackendName(settings.upscaling.backend),
                UpscaleBackendName(upscaling.activeBackend()));
    auto draft = settings;
    bool changed = false;
    if (canChangeDisplaySize()) {
      int mode = int(draft.mode);
      const char *modes[]{"Windowed", "Borderless fullscreen",
                          "Exclusive fullscreen"};
      if (ImGui::Combo("Window mode", &mode, modes, 3)) {
        draft.mode = WindowMode(mode);
        changed = true;
      }
      const std::string size =
          std::to_string(draft.width) + " x " + std::to_string(draft.height);
      if (ImGui::BeginCombo("Resolution", size.c_str())) {
        constexpr int sizes[][2]{{960, 540},  {960, 672},   {1280, 720},
                                 {1600, 900}, {1920, 1080}, {2560, 1440}};
        for (const auto &resolution : sizes) {
          const auto label = std::to_string(resolution[0]) + " x " +
                             std::to_string(resolution[1]);
          if (ImGui::Selectable(label.c_str(),
                                draft.width == resolution[0] &&
                                    draft.height == resolution[1])) {
            draft.width = draft.fullscreenWidth = resolution[0];
            draft.height = draft.fullscreenHeight = resolution[1];
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
      int count = 0;
      SDLMemoryPtr<SDL_DisplayID> displays(SDL_GetDisplays(&count));
      const auto displayLabel = "Display " + std::to_string(draft.display + 1);
      if (displays && ImGui::BeginCombo("Display", displayLabel.c_str())) {
        for (int i = 0; i < count; ++i) {
          const char *name = SDL_GetDisplayName(displays.get()[i]);
          const auto label =
              std::to_string(i + 1) + ": " + (name ? name : "Display");
          if (ImGui::Selectable(label.c_str(), i == draft.display)) {
            draft.display = i;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
      changed =
          ImGui::Checkbox("Resizable window", &draft.resizable) || changed;
    }
    changed = ImGui::Checkbox("VSync", &draft.vsync) || changed;
    ImGui::SeparatorText("Texture upscaling");
    constexpr UpscaleBackend backends[]{
        UpscaleBackend::Auto, UpscaleBackend::Linear, UpscaleBackend::MPV,
        UpscaleBackend::MetalFX, UpscaleBackend::None};
    if (ImGui::BeginCombo("Backend",
                          UpscaleBackendName(draft.upscaling.backend))) {
      for (auto backend : backends) {
        if (!upscaling.supports(backend))
          continue;
        if (ImGui::Selectable(UpscaleBackendName(backend),
                              backend == draft.upscaling.backend)) {
          draft.upscaling.backend = backend;
          changed = true;
        }
      }
      ImGui::EndCombo();
    }
    if (engine.GetUpscaler().resolve(draft.upscaling.backend) ==
            UpscaleBackend::MPV &&
        ImGui::BeginCombo("Preset (OpenGL)",
                          UpscalePresetName(draft.upscaling.preset))) {
      for (auto preset : UpscalePresets())
        if (ImGui::Selectable(UpscalePresetName(preset),
                              preset == draft.upscaling.preset)) {
          draft.upscaling.preset = preset;
          draft.upscaling.shaderFiles.clear();
          draft.upscaling.factor = std::max(2.0f, draft.upscaling.factor);
          if (draft.upscaling.backend == UpscaleBackend::None)
            draft.upscaling.backend = UpscaleBackend::Auto;
          changed = true;
        }
      ImGui::EndCombo();
    }
    if (upscaling.resolve(draft.upscaling.backend) == UpscaleBackend::MPV)
      changed =
          shaderPicker->draw(&engine.GetWindow(), draft.upscaling) || changed;
    // 1.0 disables upscaling; anything above enables the backend.
    ImGui::SliderFloat("Factor", &upscaleFactor, 1, 4,
                       upscaleFactor <= 1 ? "Off (1.00x)" : "%.2fx");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      draft.upscaling.factor = upscaleFactor;
      changed = true;
    } else if (!ImGui::IsItemActive())
      upscaleFactor = draft.upscaling.factor;
    if (changed) {
      try {
        engine.applySettings(draft);
        status = "Display / upscaling settings applied";
        statusError = false;
      } catch (const std::exception &error) {
        status = error.what();
        statusError = true;
      }
      upscaleFactor = engine.GetSettings().upscaling.factor;
    }
    if (ImGui::Button("Open game settings")) {
      visible = false;
      engine.openSettings();
    }
    ImGui::TextWrapped("Renderer selection requires restart (--renderer). "
                       "F11 / Alt+Enter toggles borderless fullscreen.");
  }
  ImGuiTheme::cardEnd();
}

void DebugOverlay::drawDialogueTools() {
  ImGuiTheme::cardBegin("card_dialogue", false, nullptr);
  {
    ImGuiTheme::sectionHeader("Dialogue appearance");
    auto draft = engine.GetSettings();
    bool changed = false;
    if (availableVNFonts.empty())
      availableVNFonts = VNFontOptions();
    const std::string selectedFont = draft.vnFontPath.empty()
                                         ? "Original / default game font"
                                         : draft.vnFontPath;
    if (ImGui::BeginCombo("VN font", selectedFont.c_str())) {
      for (const auto &font : availableVNFonts) {
        const auto label =
            font.empty() ? std::string("Original / default game font") : font;
        if (ImGui::Selectable(label.c_str(), font == draft.vnFontPath)) {
          draft.vnFontPath = font;
          changed = true;
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::SmallButton("Refresh available fonts"))
      availableVNFonts = VNFontOptions();
    changed = ImGui::Checkbox("Override dialogue colors",
                              &draft.vnTextColorOverride) ||
              changed;
    auto color = [&](const char *label, SDL_Color &value) {
      float rgba[]{value.r / 255.f, value.g / 255.f, value.b / 255.f,
                   value.a / 255.f};
      if (!ImGui::ColorEdit4(label, rgba))
        return false;
      value = {Uint8(std::clamp(rgba[0], 0.f, 1.f) * 255),
               Uint8(std::clamp(rgba[1], 0.f, 1.f) * 255),
               Uint8(std::clamp(rgba[2], 0.f, 1.f) * 255),
               Uint8(std::clamp(rgba[3], 0.f, 1.f) * 255)};
      return true;
    };
    if (draft.vnTextColorOverride) {
      changed = color("Dialogue text", draft.vnTextColor) || changed;
      changed = color("Speaker name", draft.vnSpeakerColor) || changed;
    }
    if (changed)
      try {
        engine.applySettings(draft);
        status = "Dialogue appearance applied";
        statusError = false;
      } catch (const std::exception &error) {
        status = error.what();
        statusError = true;
      }
    ImGui::TextWrapped("These engine settings are available even when a port "
                       "has no in-game Settings screen.");
  }
  ImGuiTheme::cardEnd();
}

void DebugOverlay::drawLogs() {
  ImGuiTheme::cardBegin("card_logs", false, nullptr);
  {
    ImGui::InputText("Filter", logFilter, sizeof(logFilter));
    const char *priorities[]{"Trace",   "Verbose", "Debug",   "Info",
                             "Warning", "Error",   "Critical"};
    int priority = minimumLogPriority - SDL_LOG_PRIORITY_TRACE;
    if (ImGui::Combo("Minimum priority", &priority, priorities, 7))
      minimumLogPriority = priority + SDL_LOG_PRIORITY_TRACE;
    ImGui::Checkbox("Follow", &followLogs);
    ImGui::SameLine();
    if (ImGui::Button("Clear logs"))
      Log::clear();
    if (ImGui::BeginTable("engine logs", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_NoSavedSettings |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 300))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed,
                              80.0f);
      ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed,
                              70.0f);
      ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      const auto entries = Log::entries();
      std::vector<const LogEntry *> shown;
      shown.reserve(entries.size());
      for (const auto &entry : entries)
        if (entry.priority >= minimumLogPriority &&
            (!*logFilter || entry.message.find(logFilter) != std::string::npos))
          shown.push_back(&entry);
      ImGuiListClipper logClipper;
      logClipper.Begin(static_cast<int>(shown.size()));
      while (logClipper.Step())
        for (int i = logClipper.DisplayStart; i < logClipper.DisplayEnd; ++i) {
          const auto &entry = *shown[static_cast<std::size_t>(i)];
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextDisabled("%llu ms", entry.milliseconds);
          ImGui::TableSetColumnIndex(1);
          const bool tinted = entry.priority != SDL_LOG_PRIORITY_INFO;
          if (tinted)
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  entry.priority >= SDL_LOG_PRIORITY_ERROR
                                      ? ImGuiTheme::error()
                                  : entry.priority == SDL_LOG_PRIORITY_WARN
                                      ? ImGuiTheme::warning()
                                      : ImGuiTheme::muted());
          const char *priorityName =
              entry.priority <= SDL_LOG_PRIORITY_TRACE   ? "Trace"
              : entry.priority == SDL_LOG_PRIORITY_DEBUG ? "Debug"
              : entry.priority == SDL_LOG_PRIORITY_INFO  ? "Info"
              : entry.priority == SDL_LOG_PRIORITY_WARN  ? "Warning"
              : entry.priority == SDL_LOG_PRIORITY_ERROR ? "Error"
                                                         : "Critical";
          ImGui::TextUnformatted(priorityName);
          if (tinted)
            ImGui::PopStyleColor();
          ImGui::TableSetColumnIndex(2);
          ImGui::TextDisabled("%d", entry.category);
          ImGui::TableSetColumnIndex(3);
          ImGui::TextWrapped("%s", entry.message.c_str());
        }
      if (followLogs)
        ImGui::SetScrollHereY(1);
      ImGui::EndTable();
    }
  }
  ImGuiTheme::cardEnd();
}

} // namespace Kotonoha
