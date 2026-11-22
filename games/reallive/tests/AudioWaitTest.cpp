// SPDX-License-Identifier: MIT
#include "RealLiveAudioWait.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
}

int main() {
  try {
    using RealLive::AudioWait;
    AudioWait wait;
    int probes = 0;
    const auto playingChannel = [&](int channel) {
      ++probes;
      return channel == 7;
    };
    require(!wait.active() && !wait.blocks(true, playingChannel) &&
                probes == 0,
            "idle VM never queries PCM playback");

    wait.forVoice(true);
    require(wait.active() && wait.kind() == AudioWait::Kind::Voice,
            "koeWait starts a cooperative voice barrier");
    require(wait.blocks(true, playingChannel) && probes == 0,
            "voice barrier blocks while decoded audio is playing");
    require(!wait.blocks(false, playingChannel) && !wait.active(),
            "voice barrier releases on actual audio completion");

    wait.forVoice(false);
    require(!wait.active(), "wait for missing voice must not deadlock");

    wait.forPcm(7, true);
    require(wait.channel() == 7 && wait.kind() == AudioWait::Kind::Pcm,
            "wavWait tracks the explicitly selected PCM channel");
    require(wait.blocks(false, playingChannel),
            "another voice's status cannot release PCM wait");
    require(probes == 1, "PCM wait probes only the selected channel");
    require(!wait.blocks(false, [&](int channel) {
      return channel != 7;
    }) && !wait.active(), "PCM barrier releases when its channel ends");

    wait.forPcm(-2, false);
    require(!wait.active(), "absent channel wait must not suspend scripts");
    wait.forPcm(7, true);
    wait.clear();
    require(!wait.active(), "save/load, return-to-title and shutdown reset waits");

    // Replacing a voice wait with another wait never leaves two pending
    // barriers behind. Scripts can wait only for their most recent sound.
    wait.forVoice(true);
    wait.forPcm(0, true);
    require(wait.kind() == AudioWait::Kind::Pcm &&
                !wait.blocks(true, [](int) { return false; }),
            "new PCM wait replaces earlier voice wait");
    std::cout << "RealLive synchronous voice and PCM waits passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive audio wait regression: " << error.what() << '\n';
    return 1;
  }
}
