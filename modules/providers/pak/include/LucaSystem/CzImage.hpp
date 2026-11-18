#pragma once
// Shared by the provider and native atlas layout consumers.

#include <cstdint>
#include <vector>

namespace LucaSystem {

struct CzImage {
  int width = 0;
  int height = 0;
  int cropX = 0;
  int cropY = 0;
  int cropWidth = 0;
  int cropHeight = 0;
  int canvasWidth = 0;
  int canvasHeight = 0;
  int anchorX = 0;
  int anchorY = 0;
  std::vector<std::uint8_t> rgba;
};

// Decode CZ0 (raw RGBA), CZ1 (palette/LZW), CZ2 (8-bit glyph/alpha LZW),
// CZ3 (interleaved delta) and CZ4 (split RGB/alpha) formats used by
// LucaSystem / AIR.
CzImage DecodeCzImage(const std::vector<std::uint8_t> &data);

} // namespace LucaSystem
