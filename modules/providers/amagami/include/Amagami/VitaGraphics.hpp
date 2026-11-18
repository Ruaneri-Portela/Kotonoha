#pragma once
// Headerless palette-indexed pictures from decrypted Amagami PS Vita GRAPH.CPK.
// Source format documented by hokagocafe/open-amagami (MIT).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Amagami::Vita {
struct PictureLayout {
  uint32_t width = 0, height = 0;
  bool bgra = false;
};
struct Picture {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba; // Standard RGBA8, with alpha 0..255.
};
// Does not claim support for Vita font (4-bit) or sysgraph containers.
std::optional<PictureLayout> pictureLayout(const std::string &resourceName);
// 1024-byte, 256-colour palette followed by exactly width*height indices.
// The on-disk extension is NOT a standard Sony GXT/TIM2 container.
Picture decodeIndexed8(std::string_view bytes, PictureLayout layout);
Picture decodeVitaPicture(const std::string &resourceName,
                          std::string_view bytes);
} // namespace Amagami::Vita
