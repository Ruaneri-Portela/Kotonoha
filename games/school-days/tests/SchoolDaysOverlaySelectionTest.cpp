#include "../src/SchoolDaysOverlaySelection.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {
void Check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void Write(const fs::path &file) {
  fs::create_directories(file.parent_path());
  std::ofstream output(file, std::ios::binary);
  output << "synthetic";
  Check(bool(output), "Cannot write synthetic Display Options fixture");
}

void MakeDisplayPair(const fs::path &root) {
  Write(root / "System/Option/Option_Display.png");
  Write(root / "System/Option/Option_Display.cmap");
}
} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto fixture =
      fs::temp_directory_path() /
      ("school-days-overlay-selection-" + std::to_string(nonce));
  fs::create_directories(fixture);
  try {
    const auto standard = fixture / "overlay";
    const auto legacy = fixture / "display-options";
    const auto explicitChoice = fixture / "explicit-other-overlay";

    Check(SchoolDays::OverlaySelection::Select(fixture).empty(),
          "Missing overlays should not select an unrelated directory");
    MakeDisplayPair(legacy);
    Check(SchoolDays::OverlaySelection::Select(fixture) == legacy,
          "Complete legacy overlay must work when standard is absent");

    Write(standard / "System/Option/Option_Display.png");
    Check(SchoolDays::OverlaySelection::Select(fixture) == legacy,
          "Incomplete standard pair must not override complete legacy art");
    MakeDisplayPair(standard);
    Check(SchoolDays::OverlaySelection::Select(fixture) == standard,
          "Standard overlay must beat the legacy directory");
    Check(SchoolDays::OverlaySelection::Select(fixture, {}, false).empty(),
          "Explicit --no-asset-overlay must disable automatic selection");

    Check(SchoolDays::OverlaySelection::Select(fixture, explicitChoice) ==
              explicitChoice,
          "Explicit CLI/scripts/settings path must override both defaults");
    Check(SchoolDays::OverlaySelection::Select({}, explicitChoice) ==
              explicitChoice,
          "Explicit overlay must not depend on the executable's directory");

    fs::remove(standard / "System/Option/Option_Display.cmap");
    Check(SchoolDays::OverlaySelection::Select(fixture) == legacy,
          "Incomplete standard CMAP must fall back to legacy pair");
    fs::remove(legacy / "System/Option/Option_Display.png");
    Check(SchoolDays::OverlaySelection::Select(fixture).empty(),
          "Incomplete overlays must not enable partial Display Options");
  } catch (...) {
    std::error_code ignored;
    fs::remove_all(fixture, ignored);
    throw;
  }
  std::error_code ignored;
  fs::remove_all(fixture, ignored);
  return 0;
}
