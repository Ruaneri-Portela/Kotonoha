#pragma once
// Static, nonexecuting inspection of a KiriKiri TJS2 bootstrap script.
#include <cstddef>
#include <string>
#include <vector>

namespace Kotonoha::Providers {
class KirikiriAssets;

struct TjsBootstrapReport {
  std::string entry;
  std::vector<std::string> staticallyReferenced;
  std::vector<std::string> missing;
  std::size_t dynamicCalls = 0;
  std::string error;
  // Intentionally no "start()" method: interpreting TJS2 requires a VM,
  // object model, native windowing and game-specific plugins.
};

// Examine actual scripts inside the game's effective XP3/loose provider.
// Only recognizes literal string arguments to common storage/link calls;
// it never executes script text or guesses dynamically constructed names.
TjsBootstrapReport inspectTjsBootstrap(const KirikiriAssets &assets);

} // namespace Kotonoha::Providers
