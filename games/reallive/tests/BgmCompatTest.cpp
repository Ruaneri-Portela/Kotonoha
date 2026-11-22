// SPDX-License-Identifier: MIT
#include "RealLiveBgmCompat.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
}

int main() {
  try {
    using RealLive::BgmCommandKind;
    using RealLive::bgmCommandKind;
    require(bgmCommandKind(4, 0) == BgmCommandKind::Playing &&
                bgmCommandKind(8, 0) == BgmCommandKind::Playing,
            "native and port BGM playing queries");
    require(bgmCommandKind(7, 0) == BgmCommandKind::Status,
            "native BGM status query");
    require(bgmCommandKind(9, 0) == BgmCommandKind::Gain &&
                bgmCommandKind(11, 0) == BgmCommandKind::Gain,
            "legacy and native volume query aliases");
    require(bgmCommandKind(11, 1) == BgmCommandKind::TrackName,
            "one-parameter track output preserves prior port support");
    require(bgmCommandKind(10, 0) == BgmCommandKind::Resume &&
                bgmCommandKind(10, 1) == BgmCommandKind::SetGain,
            "volume setter and unpause distinguish argument count");
    require(bgmCommandKind(12, 0) == BgmCommandKind::Pause &&
                bgmCommandKind(12, 1) == BgmCommandKind::SetGain &&
                bgmCommandKind(12, 2) == BgmCommandKind::SetGain,
            "legacy pause and native set-volume/fade variants");
    require(bgmCommandKind(13, 0) == BgmCommandKind::Resume &&
                bgmCommandKind(13, 1) == BgmCommandKind::Unmute,
            "native fade-to-full-volume and legacy resume");
    require(bgmCommandKind(14, 0) == BgmCommandKind::Stop &&
                bgmCommandKind(14, 1) == BgmCommandKind::Mute,
            "native mute and legacy stop remain distinct");
    require(bgmCommandKind(5, 0) == BgmCommandKind::Stop &&
                bgmCommandKind(6, 0) == BgmCommandKind::Stop,
            "two stop forms supported");
    for (int opcode : {0, 1, 2, 3, 1002, 101, 102, 105})
      require(bgmCommandKind(opcode, 0) == BgmCommandKind::Other,
              "play, seek and time fades keep their dedicated handlers");
    std::cout << "RealLive cross-edition BGM command table passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive BGM compatibility regression: " << error.what()
              << '\n';
    return 1;
  }
}
