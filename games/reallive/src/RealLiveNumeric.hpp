// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace RealLive {

// RealLive variables are signed 32-bit integers. Keep wrap semantics
// deterministic on macOS, Windows, Linux and Emscripten, regardless of the
// host compiler's conversion from oversized signed values.
inline int wrapInteger32(std::int64_t value) noexcept {
  const auto bits = static_cast<std::uint32_t>(value);
  if (bits <= static_cast<std::uint32_t>(
                  std::numeric_limits<std::int32_t>::max()))
    return static_cast<int>(bits);
  return static_cast<int>(static_cast<std::int64_t>(bits) -
                          INT64_C(4294967296));
}

inline int safeNegate(int value) noexcept {
  return wrapInteger32(-static_cast<std::int64_t>(value));
}

inline int safeMod(int numerator, int denominator) noexcept {
  // INT_MIN % -1 is undefined in native C++, but mathematically zero.
  return denominator == 0 || denominator == -1
             ? 0
             : numerator % denominator;
}

inline int safePercent(int numerator, int denominator) noexcept {
  if (denominator == 0)
    return 0;
  return wrapInteger32(static_cast<std::int64_t>(numerator) * 100 /
                       denominator);
}

// Unbiased uniform range sampling using only the host's existing rand()
// stream (or a deterministic supplied generator in tests). Unlike
// rand() % (upper - lower + 1), this supports the entire INT_MIN..INT_MAX
// interval and never performs signed subtraction overflow.
// The rejection step also avoids bias for ranges not dividing RAND_MAX+1.
template <class Next>
int randomInclusive(int lower, int upper, int randomMax, Next next) {
  if (lower > upper)
    std::swap(lower, upper);
  if (lower == upper)
    return lower;
  if (randomMax < 1)
    throw std::invalid_argument("RealLive RNG must have at least two outcomes");

  const auto width = static_cast<std::uint64_t>(
      static_cast<std::int64_t>(upper) -
      static_cast<std::int64_t>(lower) + 1);
  const auto base = static_cast<std::uint64_t>(randomMax) + 1;
  // Even at RAND_MAX=INT_MAX, two digits fit into a uint64_t.
  for (;;) {
    std::uint64_t sample = 0, space = 1;
    do {
      const int draw = next();
      if (draw < 0 || draw > randomMax)
        throw std::out_of_range("RealLive RNG returned an invalid value");
      sample = sample * base + static_cast<std::uint64_t>(draw);
      space *= base;
    } while (space < width);
    const auto accepted = (space / width) * width;
    if (sample < accepted)
      return static_cast<int>(
          static_cast<std::int64_t>(lower) +
          static_cast<std::int64_t>(sample % width));
  }
}

} // namespace RealLive
