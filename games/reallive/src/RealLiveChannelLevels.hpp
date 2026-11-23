// SPDX-License-Identifier: MIT
#pragma once

#include "RealLiveAudioFade.hpp"

#include <algorithm>
#include <cstdint>
#include <map>

namespace RealLive {

// Game-script channel volumes live independently of the SDL audio channel
// settings. A new PCM clip inherits the level set before it started, and
// fade progress is measured by active script time rather than wall-clock time.
class ChannelLevels {
  struct Level {
    float gain = 1.0f;
    float from = 1.0f;
    float target = 1.0f;
    int durationMs = 0;
    int elapsedMs = 0;
    bool stopAtTarget = false;
  };
  std::map<int, Level> channels_;

public:
  void clear() noexcept { channels_.clear(); }

  float gain(int channel) const noexcept {
    const auto found = channels_.find(channel);
    return found == channels_.end() ? 1.0f : found->second.gain;
  }
  int volume(int channel) const noexcept {
    return gainToNativeBgm(gain(channel));
  }
  bool fading(int channel) const noexcept {
    const auto found = channels_.find(channel);
    return found != channels_.end() && found->second.durationMs > 0;
  }

  void set(int channel, int nativeVolume, int durationMs = 0,
           bool stopAtTarget = false) {
    auto &level = channels_[channel];
    level.from = level.gain;
    level.target = nativeBgmToGain(nativeVolume);
    level.elapsedMs = 0;
    level.durationMs = std::max(0, durationMs);
    level.stopAtTarget = stopAtTarget;
    if (level.durationMs == 0)
      level.gain = level.target;
  }

  // Called only while script time advances. callback(channel, gain,
  // stopNow) updates a live PcmPlayer if it exists. A completed stop is
  // delivered once rather than every future update.
  template <class Apply>
  void advance(std::uint64_t deltaMs, Apply apply) {
    for (auto &[channel, level] : channels_) {
      if (level.durationMs <= 0)
        continue;
      level.elapsedMs =
          advanceBgmFade(level.elapsedMs, level.durationMs, deltaMs);
      level.gain = sampleBgmFade(
          level.from, level.target, level.elapsedMs, level.durationMs);
      const bool complete = level.elapsedMs == level.durationMs;
      apply(channel, level.gain, complete && level.stopAtTarget);
      if (complete) {
        level.durationMs = 0;
        level.stopAtTarget = false;
      }
    }
  }
};

} // namespace RealLive
