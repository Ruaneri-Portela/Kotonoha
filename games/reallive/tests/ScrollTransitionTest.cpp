// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Self-contained geometry oracle for RealLive scroll/squash/slide families.
#include "RealLiveTransition.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void sample(int style, int dir, double progress, int x, int y, bool newScene,
            int sourceX, int sourceY, const char *message) {
  const auto got =
      RealLive::sampleScrollTransition(style, dir, progress, x, y, 8, 8);
  require(got.newScene == newScene && got.x == sourceX && got.y == sourceY,
          message);
}
template <class F> void rejects(F operation, const char *message) {
  bool failed = false;
  try {
    operation();
  } catch (const std::out_of_range &) {
    failed = true;
  }
  require(failed, message);
}
} // namespace

int main() {
  try {
    // Every style must preserve the exact old and new pixels at endpoints,
    // regardless of whether its middle frames scroll or resample.
    for (int style : {15, 16, 17, 18, 20, 21}) {
      require(RealLive::isScrollTransitionStyle(style),
              "known native transition style");
      for (int direction = 0; direction < 4; ++direction) {
        for (int y = 0; y < 8; ++y)
          for (int x = 0; x < 8; ++x) {
            sample(style, direction, 0, x, y, false, x, y,
                   "old endpoint must be pixel-identical");
            sample(style, direction, 1, x, y, true, x, y,
                   "new endpoint must be pixel-identical");
          }
      }
    }
    require(!RealLive::isScrollTransitionStyle(4) &&
                !RealLive::isScrollTransitionStyle(10) &&
                !RealLive::isScrollTransitionStyle(120),
            "dither, wipe and blind retain separate render paths");

    // At 50%, the top region is new and the bottom region is old.
    // The mapped source coordinate distinguishes all six family types.
    sample(15, 0, .5, 3, 1, true, 3, 5, "scroll on / scroll off new");
    sample(15, 0, .5, 3, 6, false, 3, 2, "scroll on / scroll off old");
    sample(16, 0, .5, 3, 1, true, 3, 5, "scroll on / squash off new");
    sample(16, 0, .5, 3, 6, false, 3, 4, "scroll on / squash off old");
    sample(17, 0, .5, 3, 1, true, 3, 2, "squash on / scroll off new");
    sample(17, 0, .5, 3, 6, false, 3, 2, "squash on / scroll off old");
    sample(18, 0, .5, 3, 1, true, 3, 2, "squash on / squash off new");
    sample(18, 0, .5, 3, 6, false, 3, 4, "squash on / squash off old");
    sample(20, 0, .5, 3, 1, true, 3, 5, "slide on moving new");
    sample(20, 0, .5, 3, 6, false, 3, 6, "slide on stationary old");
    sample(21, 0, .5, 3, 1, true, 3, 1, "slide off stationary new");
    sample(21, 0, .5, 3, 6, false, 3, 2, "slide off moving old");

    for (int style : {15, 16, 17, 18, 20, 21}) {
      // A reversed direction swaps which side is revealed first.
      const int bottomSample =
          (style == 17 || style == 18 || style == 21) ? 7 : 3;
      sample(style, 1, .5, 4, 7, true, 4, bottomSample,
             "bottom-to-top samples the correct new image edge");
      require(
          RealLive::sampleScrollTransition(style, 1, .5, 4, 0, 8, 8).newScene ==
              false,
          "bottom-to-top leaves the top unrevealed");
      require(
          RealLive::sampleScrollTransition(style, 2, .5, 0, 4, 8, 8).newScene &&
              !RealLive::sampleScrollTransition(style, 2, .5, 7, 4, 8, 8)
                   .newScene,
          "left-to-right reveals the left edge first");
      require(
          RealLive::sampleScrollTransition(style, 3, .5, 7, 4, 8, 8).newScene &&
              !RealLive::sampleScrollTransition(style, 3, .5, 0, 4, 8, 8)
                   .newScene,
          "right-to-left reveals the right edge first");
    }

    const auto notNumber = std::numeric_limits<double>::quiet_NaN();
    sample(20, 0, notNumber, 3, 2, false, 3, 2,
           "non-finite elapsed time remains on old frame");
    sample(21, 2, -100, 2, 3, false, 2, 3, "negative elapsed time is clamped");
    sample(17, 3, 100, 2, 3, true, 2, 3, "overlong elapsed time is clamped");
    rejects(
        [] { (void)RealLive::sampleScrollTransition(99, 0, .5, 2, 3, 8, 8); },
        "unknown style rejected");
    rejects(
        [] { (void)RealLive::sampleScrollTransition(15, 0, .5, -1, 3, 8, 8); },
        "invalid screen position rejected");

    std::cout << "RealLive scroll/squash/slide regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive scroll transition regression: " << error.what()
              << '\n';
    return 1;
  }
}
