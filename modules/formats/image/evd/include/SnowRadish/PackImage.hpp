#pragma once
// Image format decoders for the Overflow ADV3 engine.
#include <cstdint>
#include <vector>

namespace SnowRadish {

struct Image {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgb; // top-down, 3 bytes per pixel (R, G, B)
};

// Decodes an .EVD picture (type 3: u32 type, width, height + delta/run/literal
// tokens in BGR, stored bottom-up). Returns false on malformed data.
// When hasHeader is false (Pure Mail CRGB files), the data is raw EVD type-3
// delta data without the 12-byte header; 800x600 is assumed.
bool decodeEvd(const std::vector<uint8_t> &data, Image &out,
               bool hasHeader = true);

struct AlphaMask {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> alpha;
};

// ALPF is a 200x150 byte mask; ALPA is a 250x200 byte mask.
bool decodeAlpha(const std::vector<uint8_t> &data, bool alternate,
                 AlphaMask &out);

} // namespace SnowRadish
