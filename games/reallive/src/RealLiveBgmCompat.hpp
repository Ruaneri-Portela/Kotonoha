// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

namespace RealLive {

// RealLive releases do not use a single universal BGM opcode table. The
// existing Kotonoha mappings are the default for backwards compatibility.
// Upstream rlvm's table may be requested explicitly per game; parameter
// count alone cannot distinguish ambiguous script opcodes.
enum class BgmCommandDialect {
  KotonohaLegacy,
  Rlvm
};

enum class BgmCommandKind {
  Other,
  Playing,
  Status,
  Gain,
  HasTrack,
  TrackName,
  SetGain,
  Pause,
  Resume,
  Stop,
  Unmute,
  Mute
};

inline BgmCommandKind bgmCommandKind(
    int opcode, std::size_t parameterCount,
    BgmCommandDialect dialect = BgmCommandDialect::KotonohaLegacy) noexcept {
  if (dialect == BgmCommandDialect::Rlvm) {
    // rlvm SoundSystem/Module_Bgm: bgmPause=9, bgmUnPause=10,
    // bgmVolume=11, bgmSetVolume=12, bgmUnMute=13, bgmMute=14.
    // These conflict with pre-existing Kotonoha mappings, so never infer
    // this dialect from the number of bytecode operands.
    switch (opcode) {
    case 4: case 8: return BgmCommandKind::Playing;
    case 7: return BgmCommandKind::Status;
    case 9: return BgmCommandKind::Pause;
    case 10: return BgmCommandKind::Resume;
    case 11: return BgmCommandKind::Gain;
    case 12: return parameterCount ? BgmCommandKind::SetGain
                                   : BgmCommandKind::Other;
    case 13: return BgmCommandKind::Unmute;
    case 14: return BgmCommandKind::Mute;
    case 5: case 6: return BgmCommandKind::Stop;
    default: return BgmCommandKind::Other;
    }
  }

  // Preserve every opcode's behavior from before the cross-edition patch
  // (notably pause/resume/stop with or without extra operands).
  switch (opcode) {
  case 4:
  case 8:
    return BgmCommandKind::Playing;
  case 7:
    return BgmCommandKind::Status;
  case 9:
    return BgmCommandKind::Gain;
  case 10:
    return BgmCommandKind::SetGain;
  case 11:
    return parameterCount ? BgmCommandKind::TrackName
                          : BgmCommandKind::HasTrack;
  case 12:
    return BgmCommandKind::Pause;
  case 13:
    return BgmCommandKind::Resume;
  case 14:
  case 5:
  case 6:
    return BgmCommandKind::Stop;
  default:
    return BgmCommandKind::Other;
  }
}

} // namespace RealLive
