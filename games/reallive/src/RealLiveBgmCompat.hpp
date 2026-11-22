// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

namespace RealLive {

// RealLive generations overload BGM opcodes with different argument forms.
// Resolve unambiguous parameter-count variants while retaining the current
// Kotonoha aliases for editions whose command tables differ from rlvm.
enum class BgmCommandKind {
  Other,
  Playing,
  Status,
  Gain,
  TrackName,
  SetGain,
  Pause,
  Resume,
  Stop,
  Unmute,
  Mute
};

inline BgmCommandKind bgmCommandKind(int opcode,
                                     std::size_t parameterCount) noexcept {
  switch (opcode) {
  case 4:
  case 8:
    return BgmCommandKind::Playing;
  case 7:
    return BgmCommandKind::Status;
  case 9:
    // Retained Kotonoha volume-query alias. In some older command tables
    // opcode 9 is instead BGM pause; edition detection is still needed.
    return BgmCommandKind::Gain;
  case 11:
    return parameterCount == 0 ? BgmCommandKind::Gain
                               : BgmCommandKind::TrackName;
  case 10:
    return parameterCount == 0 ? BgmCommandKind::Resume
                               : BgmCommandKind::SetGain;
  case 12:
    return parameterCount == 0 ? BgmCommandKind::Pause
                               : BgmCommandKind::SetGain;
  case 13:
    return parameterCount == 0 ? BgmCommandKind::Resume
                               : BgmCommandKind::Unmute;
  case 14:
    return parameterCount == 0 ? BgmCommandKind::Stop
                               : BgmCommandKind::Mute;
  case 5:
  case 6:
    return BgmCommandKind::Stop;
  default:
    return BgmCommandKind::Other;
  }
}

} // namespace RealLive
