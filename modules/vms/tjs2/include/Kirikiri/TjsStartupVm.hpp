#pragma once
// An intentionally bounded, non-native TJS2 source bootstrap interpreter.
// This is an initial execution subset, not the full Kirikiri TJS2 VM.
#include <cstddef>
#include <string>
#include <vector>

namespace Kotonoha::Providers {
class KirikiriAssets;

struct TjsStartupResult {
  // True when every visited TJS2 statement was handled by the subset VM.
  // Native UI, plugins, object/class definitions and bytecode are NOT handled.
  bool subsetCompleted = false;
  bool requiresNativeRuntime = false;
  std::size_t statements = 0;
  std::vector<std::string> visitedScripts;
  std::vector<std::string> requestedPlugins;
  std::vector<std::string> missing;
  std::string kagEntry; // A KAG target referenced by an executed TJS call.
  std::string title;    // Optional System.title assignment.
  std::string error;
};

// Evaluate only supported TJS2 bootstrap expressions, variables, control
// flow and safe native storage calls. No arbitrary system/plugin execution.
// Always stops on unsupported syntax instead of silently skipping it.
TjsStartupResult runTjsStartupSubset(const KirikiriAssets &assets,
                                    std::size_t instructionBudget = 4096);

} // namespace Kotonoha::Providers
