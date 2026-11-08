#pragma once

#include <algorithm>
#include <array>

namespace RealLive {
inline bool transitionRevealsNewPixel(int style, double progress, int x, int y) {
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
} // namespace RealLive
