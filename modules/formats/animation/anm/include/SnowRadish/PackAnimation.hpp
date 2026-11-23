#pragma once
// Animation/sprite format decoders for the Overflow ADV3 engine.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SnowRadish {

struct Sprite {
  uint32_t width = 0, height = 0; // size of the cropped picture
  int32_t x = 0, y = 0;           // its position on the 800x600 screen canvas
  std::vector<uint8_t> rgba;      // top-down RGBA
};

struct Animation {
  struct Region {
    uint32_t actor = 0;
    size_t first = 0, count = 0;
  };
  std::vector<Sprite> frames;
  std::vector<Region> regions;
  bool cursor = false;
};

// Decodes lip-sync region tables and cursor CHD offset/size tables.
bool decodeAnm(const std::vector<uint8_t> &data, Animation &out);

// Decodes a type-1 .CHD sprite (LOCPU.DLL LoadChr): a 20-byte header (bounding
// box), then 600 screen rows stored bottom-up. Row tokens: 0xFF end of row,
// >=0x9F skip n-0x9E, 0x7F..0x9E opaque run of n-0x7E, <0x7F one pixel with
// alpha n/0x67. The result is cropped to its visible pixels.
bool decodeChd(const std::vector<uint8_t> &data, Sprite &out);
// Headerless, bottom-up 800x600 sprite stream used by Pure Mail.
bool decodeChar(const std::vector<uint8_t> &data, Sprite &out);

} // namespace SnowRadish
