#pragma once

#include <cstdint>
#include <string>
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

// RealLive ANM objects reference a G00/PDT image and describe animation frames
// as crops of that image plus an absolute screen destination.
struct AnmFrame {
  std::int32_t x1{}, y1{}, x2{}, y2{}, destX{}, destY{}, time{};
};
struct Anm {
  std::string image;
  std::vector<AnmFrame> frames;
  std::vector<std::vector<int>> frameLists;
  std::vector<std::vector<int>> sets;
};
Anm decodeANM(const std::vector<std::uint8_t> &bytes);

} // namespace RealLive
