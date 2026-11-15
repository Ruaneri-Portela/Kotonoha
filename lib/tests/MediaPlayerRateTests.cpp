#include <Kotonoha/media/MediaPlayer.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

bool rejected(Kotonoha::MediaPlayer &player, double rate) {
  try {
    player.setPlaybackRate(rate);
    return false;
  } catch (const std::invalid_argument &) {
    return true;
  }
}
} // namespace

int main() {
  try {
    Kotonoha::MediaPlayer player;
    require(player.playbackRate() == 1.0, "Media playback rate must default to 1x");
    for (double rate : {0.01, 1.0, 2.0, 4.0, 12.0, 24.0, 100.0}) {
      player.setPlaybackRate(rate);
      require(player.playbackRate() == rate, "Media playback rate did not latch");
    }
    require(rejected(player, 0.0) && rejected(player, 0.009) &&
                rejected(player, 100.001) &&
                rejected(player, std::numeric_limits<double>::infinity()) &&
                rejected(player, std::numeric_limits<double>::quiet_NaN()),
            "Media playback-rate bounds must fail closed");
    require(player.playbackRate() == 100.0,
            "Rejected playback rate must not mutate the previous value");
    player.setBufferSeconds(2.0);
    require(player.bufferSeconds() == 2.0,
            "Media buffer target did not latch the commercial cache size");
    bool rejectedBuffer = false;
    try {
      player.setBufferSeconds(0.01);
    } catch (const std::invalid_argument &) {
      rejectedBuffer = true;
    }
    require(rejectedBuffer && player.bufferSeconds() == 2.0,
            "Invalid media buffer target mutated the previous value");
    std::cout << "MediaPlayer playback-rate policy passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "MediaPlayer playback-rate policy: " << error.what() << '\n';
    return 1;
  }
}
