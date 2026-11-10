#pragma once

#include <cstdint>
#include <vector>

namespace LucaSystem {

struct CzImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

// Decode CZ0 (raw RGBA), CZ1 (palette/LZW), CZ3 (interleaved delta) and
// CZ4 (split RGB/alpha)
// formats used
// by AIR's original title screen.
CzImage DecodeCzImage(const std::vector<std::uint8_t> &data);

} // namespace LucaSystem
