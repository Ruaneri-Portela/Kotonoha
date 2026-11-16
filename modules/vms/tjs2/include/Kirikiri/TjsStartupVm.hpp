#pragma once
// An intentionally bounded, non-native TJS2 source bootstrap interpreter.
// This is an initial execution subset, not the full Kirikiri TJS2 VM.
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::Providers {
class KirikiriAssets;
class TjsUiSession;

// A bounded projection of script-created Window/Layer objects. These are
// not actual platform windows: the Kotonoha runtime renders the layers in
// its novel viewport. Values use native 1280x720 scene coordinates.
struct TjsUiLayer {
  std::size_t id = 0;
  std::string storage;
  std::string onClick;
  std::string onMouseEnter;
  std::string onMouseLeave;
  int left = 0, top = 0;
  int width = 0, height = 0;
  int opacity = 255;
  int order = 0;
  bool visible = true;
  bool enabled = true;
};

struct TjsStartupResult {
  // True when every visited TJS2 statement was handled by the subset VM.
  // Supports bounded script functions/classes plus virtual Window/Layer
  // and Dictionary objects. Complete native class semantics, plugins and
  // TJS2 bytecode are NOT handled.
  bool subsetCompleted = false;
  bool requiresNativeRuntime = false;
  std::size_t statements = 0;
  std::vector<std::string> visitedScripts;
  std::vector<std::string> requestedPlugins;
  std::vector<std::string> missing;
  std::string kagEntry; // A KAG target referenced by an executed TJS call.
  std::string title;    // Optional System.title assignment.
  std::string error;
  // A scripted title can present its own drawable layers and named click
  // handlers without running native DLLs or platform Window objects.
  std::vector<TjsUiLayer> layers;
  std::shared_ptr<TjsUiSession> uiSession;
};

// Execute one topmost hit-tested named onClick handler on the retained VM.
// A callback may change layers, or request a KAG entry via Scripts.execStorage.
// Returns false for a miss or when a native/KAG scene is already active.
// The result is refreshed after callbacks, including any error diagnostics.
bool dispatchTjsUiClick(TjsStartupResult &result, float x, float y);

// Dispatch pointer enter/leave changes to retained script event handlers.
// Returns true only if a source handler was invoked.
bool dispatchTjsUiHover(TjsStartupResult &result, float x, float y);

// Evaluate only supported TJS2 bootstrap expressions, variables, control
// flow and safe native storage calls. No arbitrary system/plugin execution.
// Always stops on unsupported syntax instead of silently skipping it.
TjsStartupResult runTjsStartupSubset(const KirikiriAssets &assets,
                                    std::size_t instructionBudget = 4096);

} // namespace Kotonoha::Providers
