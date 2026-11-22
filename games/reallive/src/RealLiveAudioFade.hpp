// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace RealLive {

// RealLive BGM opcodes expose 8-bit integer volumes (0..255), whereas the
// shared Kotonoha PCM player accepts floating-point gains in [0, 1].
inline float nativeBgmToGain(int nativeVolume) {
  return static_cast<float>(std::clamp(nativeVolume, 0, 255)) / 255.0f;
}
inline int gainToNativeBgm(float gain) {
  if (!std::isfinite(gain))
    return 0;
  return static_cast<int>(std::lround(std::clamp(gain, 0.0f, 1.0f) * 255.0f));
}

// Normalized fade progress, including zero-duration operations. Calculating
// elapsed in a 64-bit intermediate avoids overflow after long frame stalls.
inline float sampleBgmFade(float from, float to, int elapsed, int duration) {
  if (duration <= 0)
    return std::clamp(to, 0.0f, 1.0f);
  const float t = static_cast<float>(
      std::clamp<std::int64_t>(elapsed, 0, duration)) /
      static_cast<float>(duration);
  return std::clamp(from + (to - from) * t, 0.0f, 1.0f);
}
inline int advanceBgmFade(int elapsed, int duration, std::uint64_t deltaMs) {
  if (duration <= 0)
    return 0;
  const auto prior = std::clamp(elapsed, 0, duration);
  const auto remaining = static_cast<std::uint64_t>(duration - prior);
  // Avoid uint64 wrap when an external clock jumps by a huge amount.
  if (deltaMs >= remaining)
    return duration;
  return prior + static_cast<int>(deltaMs);
}

}  // namespace RealLive
