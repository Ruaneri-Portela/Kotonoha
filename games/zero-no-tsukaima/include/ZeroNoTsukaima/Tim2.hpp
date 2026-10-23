#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ZeroNoTsukaima {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba; // width * height * 4
};

// Decodes the first picture of a PS2 TIM2 file (16/24/32-bit direct colour or
// 4/8-bit paletted). PS2 alpha (0x80 = opaque) is normalised to 0..255.
bool DecodeTim2(const uint8_t *data, size_t size, Image *out);

} // namespace ZeroNoTsukaima
