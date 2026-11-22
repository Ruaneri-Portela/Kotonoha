// SPDX-License-Identifier: MIT
// Regression for BGM falling silent when native and legacy RealLive opcodes
// were mixed by parameter count. Use the working legacy mapping by default.
#include "RealLiveBgmCompat.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace

int main() {
  try {
    using RealLive::BgmCommandKind;
    using RealLive::BgmCommandDialect;
    using RealLive::bgmCommandKind;
    constexpr auto legacy = BgmCommandDialect::KotonohaLegacy;
    constexpr auto rlvm = BgmCommandDialect::Rlvm;

    // Exact behavior of the established Kotonoha implementation before the
    // "cross-edition" update. Extras never turn pause/resume/stop into mute.
    for (std::size_t count : {0u, 1u, 2u, 3u, 5u}) {
      require(bgmCommandKind(12, count) == BgmCommandKind::Pause,
              "legacy opcode 12 must PAUSE, never change gain to zero");
      require(bgmCommandKind(13, count) == BgmCommandKind::Resume,
              "legacy opcode 13 must RESUME regardless of extra args");
      require(bgmCommandKind(14, count) == BgmCommandKind::Stop,
              "legacy opcode 14 must STOP, not leave a zero-gain track");
      require(bgmCommandKind(12, count, legacy) ==
                  bgmCommandKind(12, count) &&
                  bgmCommandKind(13, count, legacy) ==
                      bgmCommandKind(13, count) &&
                  bgmCommandKind(14, count, legacy) ==
                      bgmCommandKind(14, count),
              "legacy game configuration must remain the default");
    }
    require(bgmCommandKind(9, 0) == BgmCommandKind::Gain &&
                bgmCommandKind(10, 0) == BgmCommandKind::SetGain,
            "legacy gain getter/setter do not become pause/resume");
    require(bgmCommandKind(11, 0) == BgmCommandKind::HasTrack,
            "legacy opcode 11 without args returns 0/1 track presence");
    require(bgmCommandKind(11, 1) == BgmCommandKind::TrackName,
            "legacy opcode 11 with destination returns track name");
    require(bgmCommandKind(4, 0) == BgmCommandKind::Playing &&
                bgmCommandKind(8, 0) == BgmCommandKind::Playing &&
                bgmCommandKind(7, 0) == BgmCommandKind::Status,
            "BGM playback and status queries remain supported");
    for (int op : {5, 6, 14})
      require(bgmCommandKind(op, 0) == BgmCommandKind::Stop,
              "supported stop operations reset the BGM player");

    // Regression trace from a hypothetical retail title: begin BGM, issue
    // pause with an extra operand, resume with an extra operand, then stop.
    // The old broken dispatcher interpreted these as volume=0 and mute.
    bool playing = true;
    bool paused = false;
    int gain = 255;
    for (const auto &[opcode, count] :
         std::array<std::pair<int, std::size_t>, 3>{
             {{12, 1}, {13, 1}, {14, 1}}}) {
      switch (bgmCommandKind(opcode, count)) {
      case BgmCommandKind::Pause: paused = true; break;
      case BgmCommandKind::Resume: paused = false; break;
      case BgmCommandKind::Stop: playing = false; break;
      case BgmCommandKind::Mute: gain = 0; break;
      case BgmCommandKind::SetGain: gain = 0; break;
      default: break;
      }
    }
    require(!playing && !paused && gain == 255,
            "legacy sequence never leaves silent volume for the next track");

    // The rlvm opcode dialect is different; opt in explicitly per edition.
    require(bgmCommandKind(9, 0, rlvm) == BgmCommandKind::Pause &&
                bgmCommandKind(10, 0, rlvm) == BgmCommandKind::Resume &&
                bgmCommandKind(11, 0, rlvm) == BgmCommandKind::Gain,
            "explicit rlvm dialect handles pause/resume/volume opcodes");
    require(bgmCommandKind(12, 1, rlvm) == BgmCommandKind::SetGain &&
                bgmCommandKind(12, 2, rlvm) == BgmCommandKind::SetGain &&
                bgmCommandKind(12, 0, rlvm) == BgmCommandKind::Other,
            "rlvm set-volume requires an actual volume operand");
    require(bgmCommandKind(13, 0, rlvm) == BgmCommandKind::Unmute &&
                bgmCommandKind(13, 1, rlvm) == BgmCommandKind::Unmute &&
                bgmCommandKind(14, 0, rlvm) == BgmCommandKind::Mute &&
                bgmCommandKind(14, 1, rlvm) == BgmCommandKind::Mute,
            "native mute/unmute never fall through to legacy stop/resume");
    require(bgmCommandKind(5, 0, rlvm) == BgmCommandKind::Stop &&
                bgmCommandKind(6, 0, rlvm) == BgmCommandKind::Stop,
            "explicit rlvm dialect retains both stop opcodes");
    for (int opcode : {0, 1, 2, 3, 1002, 101, 102, 105})
      require(bgmCommandKind(opcode, 0) == BgmCommandKind::Other &&
                  bgmCommandKind(opcode, 0, rlvm) == BgmCommandKind::Other,
              "play, seek and fades keep their established runtime paths");

    std::cout << "RealLive BGM legacy-volume regression and explicit rlvm "
                 "dialect tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive BGM compatibility regression: " << error.what()
              << '\n';
    return 1;
  }
}
