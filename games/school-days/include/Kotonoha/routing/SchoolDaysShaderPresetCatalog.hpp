#pragma once

#include <Kotonoha/media/upscaling/Upscaler.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Kotonoha::SchoolDaysShaderPresetCatalog {

// Presentation-only grouping for the fifteen existing presets. It does not
// rename presets, rewrite settings.json or move bundled GLSL files. These
// categories indicate relative complexity, not benchmarked FPS guarantees.
enum class Tier { Low, Medium, High };
inline constexpr std::array<Tier, 3> Tiers{Tier::Low, Tier::Medium, Tier::High};

inline constexpr std::size_t Index(Tier tier) {
  return static_cast<std::size_t>(tier);
}

inline constexpr const char *Name(Tier tier) {
  switch (tier) {
  case Tier::Low: return "LOW - Performance";
  case Tier::Medium: return "MEDIUM - Quality";
  case Tier::High: return "HIGH - Ultra Quality";
  }
  return "Unknown";
}

inline constexpr Tier Group(UpscalePreset preset) {
  switch (preset) {
  case UpscalePreset::Anime4KModeAFast:
  case UpscalePreset::Anime4KModeBFast:
  case UpscalePreset::Anime4KModeCFast:
    return Tier::Low;
  case UpscalePreset::Anime4KModeAAFast:
  case UpscalePreset::Anime4KModeBBFast:
  case UpscalePreset::Anime4KModeCAFast:
  case UpscalePreset::Anime4KModeAHQ:
  case UpscalePreset::Anime4KModeBHQ:
  case UpscalePreset::Anime4KModeCHQ:
  case UpscalePreset::ArtCNN:
  case UpscalePreset::ArtCNNDenoise:
    return Tier::Medium;
  case UpscalePreset::Anime4KModeAAHQ:
  case UpscalePreset::Anime4KModeBBHQ:
  case UpscalePreset::Anime4KModeCAHQ:
  case UpscalePreset::ArtCNNLarge:
    return Tier::High;
  }
  return Tier::Medium;
}

inline bool SupportedShaderFile(const std::string &name) {
  if (name.empty()) return false;
  auto extension = std::filesystem::u8path(name).extension().string();
  for (char &letter : extension)
    if (letter >= 'A' && letter <= 'Z')
      letter = static_cast<char>(letter - 'A' + 'a');
  return extension == ".glsl" || extension == ".hook" || extension == ".frag";
}

struct Row {
  bool heading = false;
  Tier tier = Tier::Low;
  UpscalePreset preset = UpscalePreset::Anime4K;
};

inline std::vector<Row> VisibleRows(const std::array<bool, 3> &expanded) {
  std::vector<Row> result;
  for (const auto tier : Tiers) {
    result.push_back({true, tier, UpscalePreset::Anime4K});
    if (!expanded[Index(tier)]) continue;
    for (const auto preset : UpscalePresets()) {
      if (Group(preset) == tier) result.push_back({false, tier, preset});
    }
  }
  return result;
}

inline int VisibleIndexOf(UpscalePreset selected,
                          const std::array<bool, 3> &expanded) {
  const auto rows = VisibleRows(expanded);
  for (std::size_t i = 0; i < rows.size(); ++i)
    if (!rows[i].heading && rows[i].preset == selected)
      return static_cast<int>(i);
  return -1;
}

} // namespace Kotonoha::SchoolDaysShaderPresetCatalog
