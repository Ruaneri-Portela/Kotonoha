#pragma once

#include <Kotonoha/Gameplay.hpp>

namespace Kotonoha {

// Retail School Days HQ speed selector policy. The engine owns only the
// generic ability to change playback rate; this profile maps the five authored
// selector positions to their authenticated rates.
struct SchoolDaysPlaybackPolicy final {
  static bool RateForIndex(int index, double *rate) {
    static constexpr double kRates[] = {1.0, 2.0, 4.0, 12.0, 24.0};
    if (rate == nullptr || index < 0 || index >= 5) return false;
    *rate = kRates[index];
    return true;
  }

  static bool Apply(Gameplay *gameplay, int index) {
    double rate = 1.0;
    return gameplay != nullptr && RateForIndex(index, &rate) &&
           gameplay->SetPlaybackRate(rate);
  }
};

} // namespace Kotonoha
