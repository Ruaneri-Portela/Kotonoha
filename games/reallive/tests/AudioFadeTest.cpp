// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLiveAudioFade.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
  require(std::fabs(actual - expected) < 0.00002f, message);
}
}  // namespace

int main() {
  try {
    using RealLive::nativeBgmToGain;
    using RealLive::gainToNativeBgm;
    using RealLive::sampleBgmFade;
    using RealLive::advanceBgmFade;

    near(nativeBgmToGain(0), 0.0f, "native mute to PCM gain");
    near(nativeBgmToGain(255), 1.0f, "native maximum to PCM gain");
    near(nativeBgmToGain(128), 128.0f / 255.0f,
         "native intermediate volume to PCM gain");
    near(nativeBgmToGain(-100), 0.0f, "negative script volume clamps");
    near(nativeBgmToGain(999), 1.0f, "oversized script volume clamps");
    for (int volume = 0; volume <= 255; ++volume)
      require(gainToNativeBgm(nativeBgmToGain(volume)) == volume,
              "native gain conversion must round-trip for every byte");
    require(gainToNativeBgm(-1) == 0 &&
                gainToNativeBgm(5) == 255 &&
                gainToNativeBgm(std::numeric_limits<float>::quiet_NaN()) == 0,
            "queries reject invalid gain without leaving native range");

    near(sampleBgmFade(0.0f, 1.0f, 0, 1000), 0.0f,
         "fade-in starts silent");
    near(sampleBgmFade(0.0f, 1.0f, 500, 1000), 0.5f,
         "fade-in midpoint");
    near(sampleBgmFade(0.0f, 1.0f, 1000, 1000), 1.0f,
         "fade-in reaches exact target");
    near(sampleBgmFade(1.0f, 0.0f, 250, 1000), 0.75f,
         "fade-out uses normalized range");
    near(sampleBgmFade(0.2f, 0.8f, -25, 100), 0.2f,
         "negative fade time clamps to start");
    near(sampleBgmFade(0.2f, 0.8f, 120, 100), 0.8f,
         "overshot fade clamps to target");
    near(sampleBgmFade(0.0f, 0.4f, 0, 0), 0.4f,
         "zero-duration fade applies target immediately");
    near(sampleBgmFade(0.9f, 0.1f, 0, -1), 0.1f,
         "invalid fade duration has deterministic target");

    require(advanceBgmFade(0, 1000, 250) == 250 &&
                advanceBgmFade(250, 1000, 750) == 1000 &&
                advanceBgmFade(250, 1000, UINT64_MAX) == 1000 &&
                advanceBgmFade(-1, 1000, 50) == 50 &&
                advanceBgmFade(100, 0, 50) == 0,
            "fade tick advancement is bounded across long pauses");
    // Paused update never calls advanceBgmFade: source and target remain
    // unchanged and resume continues from the same interpolated value.
    const int beforePause = advanceBgmFade(0, 1000, 200);
    near(sampleBgmFade(0, 1, beforePause, 1000), 0.2f,
         "fade progress is stable before script pause");
    const int afterResume = advanceBgmFade(beforePause, 1000, 250);
    near(sampleBgmFade(0, 1, afterResume, 1000), 0.45f,
         "fade resumes without counting paused time");

    using RealLive::bgmSeekFrame;
    using RealLive::bgmResumeMilliseconds;
    require(bgmSeekFrame(0, 1000, 22050, 300000) == 22050,
            "22.05kHz music seek uses source rate");
    require(bgmSeekFrame(0, 1000, 32000, 300000) == 32000,
            "32kHz music seek uses source rate");
    require(bgmSeekFrame(0, 1000, 44100, 300000) == 44100,
            "44.1kHz seek retains original position");
    require(bgmSeekFrame(0, 1000, 48000, 300000) == 48000,
            "48kHz music seek uses source rate");
    require(bgmSeekFrame(100, 250, 48000, 300000) == 12100,
            "authored start offset combines with millisecond seek");
    require(bgmSeekFrame(0, 250000, 48000, 300000) == 300000,
            "oversized positive seek clamps to clip end");
    require(bgmSeekFrame(-1000, 5, 44100, 300000) == 0,
            "negative authored start clamps to zero");
    require(bgmSeekFrame(0, INT64_MAX, 48000, 300000) == 300000,
            "large offsets never overflow when seeking");
    require(bgmResumeMilliseconds(1.125) == 1125,
            "saved playback seconds convert to milliseconds");
    require(bgmSeekFrame(0, bgmResumeMilliseconds(1.125), 48000, 300000) ==
                54000, "saved position resumes precisely in 48kHz files");
    bool rejectedSeek = false;
    try {
      (void)bgmSeekFrame(0, -1, 44100, 300000);
    } catch (const std::invalid_argument &) {
      rejectedSeek = true;
    }
    require(rejectedSeek, "negative music offsets are invalid");
    rejectedSeek = false;
    try {
      (void)bgmSeekFrame(0, 1000, 0, 300000);
    } catch (const std::invalid_argument &) {
      rejectedSeek = true;
    }
    require(rejectedSeek, "zero rate cannot yield a frame position");
    rejectedSeek = false;
    try {
      (void)bgmResumeMilliseconds(
          std::numeric_limits<double>::infinity());
    } catch (const std::out_of_range &) {
      rejectedSeek = true;
    }
    require(rejectedSeek, "invalid save seconds rejected before playback");

    std::cout << "RealLive BGM gain/fade regressions passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "RealLive audio fade regression: " << e.what() << '\n';
    return 1;
  }
}
