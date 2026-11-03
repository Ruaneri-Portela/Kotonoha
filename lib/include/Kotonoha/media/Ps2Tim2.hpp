#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace Kotonoha {
struct Ps2Image {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;
};
// First TIM2 picture, direct colour or linear 4/8-bit pixels with CSM1/CSM2
// palettes. PS2 alpha 0x80 becomes 255. Swizzled textures need unswizzling
// first.
bool DecodePs2Tim2(const uint8_t *data, size_t size, Ps2Image *out);
} // namespace Kotonoha
