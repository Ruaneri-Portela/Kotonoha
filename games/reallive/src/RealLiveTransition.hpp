#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace RealLive {
inline bool transitionRevealsNewPixel(int style, double progress, int x,
                                      int y) {
  static constexpr std::array<std::array<unsigned char, 8>, 8> bayer{{
      {{0, 48, 12, 60, 3, 51, 15, 63}},
      {{32, 16, 44, 28, 35, 19, 47, 31}},
      {{8, 56, 4, 52, 11, 59, 7, 55}},
      {{40, 24, 36, 20, 43, 27, 39, 23}},
      {{2, 50, 14, 62, 1, 49, 13, 61}},
      {{34, 18, 46, 30, 33, 17, 45, 29}},
      {{10, 58, 6, 54, 9, 57, 5, 53}},
      {{42, 26, 38, 22, 41, 25, 37, 21}},
  }};
  const double reveal = std::clamp(progress, 0.0, 1.0) * 64.0;
  const int column = style == 5 ? (x + y) & 7 : x & 7;
  const int row = y & 7;
  return bayer[row][column] < reveal;
}
// RealLive SEL entries describe effect style, direction and interpolation.
// rlvm maps style 10 to directional wipes and style 120 to blinds. The
// implementations here are independent and operate at native scene resolution.
struct TransitionParameters {
  int style = 0;
  int direction = 0; // top, bottom, left, right = 0, 1, 2, 3
  int interpolation = 0;
  int xSize = 0;
  int ySize = 0;
};

// Fraction of the new scene visible at a native pixel. Values between 0 and
// 1 represent the anti-aliased leading edge of an interpolated wipe.
inline double transitionNewCoverage(const TransitionParameters &effect,
                                    double progress, int x, int y,
                                    int width, int height) {
  if (!std::isfinite(progress))
    return 0.0;
  const double t = std::clamp(progress, 0.0, 1.0);
  if (t <= 0.0)
    return 0.0;
  if (t >= 1.0)
    return 1.0;
  if (width <= 0 || height <= 0)
    return t;
  if (effect.style == 4 || effect.style == 5)
    return transitionRevealsNewPixel(effect.style, t, x, y) ? 1.0 : 0.0;
  if (effect.style != 10 && effect.style != 120)
    return t; // existing crossfade for as-yet unverified styles

  const int direction = (effect.direction >= 0 && effect.direction <= 3)
                            ? effect.direction : 0;
  const bool vertical = direction < 2;
  const int dimension = vertical ? height : width;
  int coordinate = vertical ? y : x;
  if (direction == 1 || direction == 3)
    coordinate = dimension - 1 - coordinate;
  if (coordinate < 0 || coordinate >= dimension)
    return 0.0;

  if (effect.style == 120) {
    // RealLive's blinds reveal multiple strips in parallel. The SIZE inputs
    // are swapped in older SEL data; match rlvm's fallback convention.
    const int requested = vertical ? (effect.xSize > 0 ? effect.xSize
                                                       : effect.ySize)
                                   : (effect.ySize > 0 ? effect.ySize
                                                       : effect.xSize);
    const int span = std::clamp(requested, 1, dimension);
    return double(coordinate % span) + 0.5 <= t * span ? 1.0 : 0.0;
  }

  const int feather = std::clamp(effect.interpolation, 0, dimension);
  const double position = (dimension + feather) * t;
  const double distance = double(coordinate) + 0.5;
  if (feather == 0)
    return distance <= position ? 1.0 : 0.0;
  return std::clamp((position - distance) / feather, 0.0, 1.0);
}
// Screen-space composition for rlvm's scroll/squash/slide effect families.
// Mapping is done in native RealLive pixels before global upscaling.
struct TransitionSourcePixel {
  bool newScene = false;
  int x = 0, y = 0;
};

inline bool isScrollTransitionStyle(int style) {
  return style == 15 || style == 16 || style == 17 ||
         style == 18 || style == 20 || style == 21;
}

inline TransitionSourcePixel sampleScrollTransition(
    int style, int direction, double progress,
    int x, int y, int width, int height) {
  if (!isScrollTransitionStyle(style) || width <= 0 || height <= 0 ||
      x < 0 || x >= width || y < 0 || y >= height)
    throw std::out_of_range("Invalid RealLive scroll transition sample");
  const double t = std::isfinite(progress)
                       ? std::clamp(progress, 0.0, 1.0) : 0.0;
  const int dir = direction >= 0 && direction <= 3 ? direction : 0;
  const bool vertical = dir < 2;
  const bool reverse = dir == 1 || dir == 3;
  const int size = vertical ? height : width;
  const int pos = vertical ? y : x;
  const int oriented = reverse ? size - 1 - pos : pos;
  const int reveal = std::clamp(static_cast<int>(t * size), 0, size);
  const bool newScene = oriented < reveal;

  int src = oriented;
  if (newScene) {
    if (style == 17 || style == 18) {
      // Squash on: expand all new pixels into the revealed portion.
      src = int(static_cast<std::int64_t>(oriented) * size / reveal);
    } else if (style != 21) {
      // Scroll on: the approaching image enters from outside the edge.
      src = oriented + size - reveal;
    }
    // Slide off (21) keeps the newly revealed background stationary.
  } else {
    if (style == 16 || style == 18) {
      // Squash off: compress the old frame into the un-revealed portion.
      src = int(static_cast<std::int64_t>(oriented - reveal) * size /
                (size - reveal));
    } else if (style != 20) {
      // Scroll off: old image exits with its source coordinates shifted.
      src = oriented - reveal;
    }
    // Slide on (20) keeps the old background stationary.
  }
  src = std::clamp(src, 0, size - 1);
  const int result = reverse ? size - 1 - src : src;
  return vertical ? TransitionSourcePixel{newScene, x, result}
                  : TransitionSourcePixel{newScene, result, y};
}

} // namespace RealLive
