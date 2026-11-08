#pragma once

#include <cstdint>
#include <vector>

namespace RealLive {

// Bounds are inclusive, as stored by RealLive. Atlas bounds can differ from
// source bounds when multiple patterns share the same rectangle.
struct ImageRegion {
  std::int32_t x1{}, y1{}, x2{}, y2{}, originX{}, originY{};
  std::int32_t atlasX{}, atlasY{};
};

struct Image {
  std::uint32_t width{}, height{};
  std::uint32_t sourceWidth{}, sourceHeight{};
  std::vector<std::uint8_t> rgba;
  std::vector<ImageRegion> regions;
};

// Throws std::runtime_error for invalid, truncated or unsupported input.
Image decodeG00(const std::vector<std::uint8_t> &bytes);
Image decodePDT(const std::vector<std::uint8_t> &bytes);

} // namespace RealLive
