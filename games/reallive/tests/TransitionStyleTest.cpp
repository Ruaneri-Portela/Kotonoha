// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLiveTransition.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void approx(double actual, double expected, const char *message) {
  require(std::abs(actual - expected) < 1e-10, message);
}
} // namespace

int main() {
  try {
    using RealLive::transitionNewCoverage;
    using RealLive::TransitionParameters;
    const auto covered = [&](const TransitionParameters &effect, double p,
                             int x, int y, int w = 8, int h = 8) {
      return transitionNewCoverage(effect, p, x, y, w, h);
    };

    // RealLive/rlvm's style 10 direction IDs: top to bottom, bottom to top,
    // left to right, and right to left.
    for (int direction = 0; direction < 4; ++direction) {
      TransitionParameters wipe{10, direction, 0, 0, 0};
      for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y) {
          approx(covered(wipe, 0.0, x, y), 0.0,
                 "wipes start at fully old frame");
          approx(covered(wipe, 1.0, x, y), 1.0, "wipes end at fully new frame");
        }
      const bool vertical = direction < 2;
      const bool reversed = direction == 1 || direction == 3;
      const int first = reversed ? 7 : 0;
      const int last = reversed ? 0 : 7;
      const int nearX = vertical ? 3 : first;
      const int nearY = vertical ? first : 3;
      const int farX = vertical ? 3 : last;
      const int farY = vertical ? last : 3;
      approx(covered(wipe, 0.5, nearX, nearY), 1.0,
             "wipe begins at requested edge");
      approx(covered(wipe, 0.5, farX, farY), 0.0,
             "opposite edge remains old at midpoint");
    }

    // An interpolated wipe is smooth only along its advancing edge.
    TransitionParameters feather{10, 2, 2, 0, 0};
    approx(covered(feather, 0.5, 3, 4), 0.75,
           "wipe feather includes partial new pixel");
    approx(covered(feather, 0.5, 4, 4), 0.25,
           "wipe feather correctly blends next pixel");
    approx(covered(feather, 0.5, 0, 4), 1.0, "fully revealed pixel stays new");
    approx(covered(feather, 0.5, 7, 4), 0.0, "unreached pixel stays old");
    approx(covered(feather, -100.0, 0, 0), 0.0, "negative progress clamps");
    approx(covered(feather, 10.0, 7, 7), 1.0, "progress overrun clamps");

    // Style 120 blinds sweep each 4-pixel stripe in parallel. The alternative
    // axis parameter is used when the native SEL has a zero size.
    TransitionParameters blind{120, 0, 0, 4, 0};
    for (int y = 0; y < 8; ++y)
      approx(covered(blind, 0.5, 4, y), y % 4 < 2 ? 1.0 : 0.0,
             "top-to-bottom blind repeats vertically");
    blind = {120, 1, 0, 0, 4};
    approx(covered(blind, 0.5, 4, 7), 1.0,
           "bottom-to-top blind inverts stripe direction");
    approx(covered(blind, 0.5, 4, 4), 0.0, "unrevealed blind stripe stays old");
    blind = {120, 2, 0, 4, 0};
    approx(covered(blind, 0.5, 0, 4), 1.0,
           "left-to-right blind uses horizontal fallback");
    approx(covered(blind, 0.5, 3, 4), 0.0,
           "left-to-right blind leaves stripe tail hidden");

    TransitionParameters crossfade{50, 0, 0, 0, 0};
    approx(covered(crossfade, 0.375, 4, 4), 0.375,
           "existing fade style preserves fractional opacity");
    TransitionParameters dither{4, 0, 0, 0, 0};
    approx(covered(dither, 0, 0, 0), 0, "ordered dither starts concealed");
    approx(covered(dither, 1, 7, 7), 1,
           "ordered dither fully reveals final frame");

    std::cout << "RealLive directional transition regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive transition regression: " << error.what() << '\n';
    return 1;
  }
}
