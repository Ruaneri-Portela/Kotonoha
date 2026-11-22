// SPDX-License-Identifier: MIT
#include "RealLiveChannelLevels.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void near(float actual, float expected, const char *message) {
  if (std::fabs(actual - expected) > 0.00002f)
    throw std::runtime_error(message);
}
struct Event {
  int channel = 0;
  float gain = 0;
  bool stop = false;
};
} // namespace

int main() {
  try {
    RealLive::ChannelLevels levels;
    require(levels.volume(1) == 255 && levels.volume(99) == 255,
            "unknown script channels retain full default volume");
    levels.set(3, 64);
    require(levels.volume(3) == 64 && levels.volume(4) == 255,
            "volume is scoped to an individual PCM channel");
    near(levels.gain(3), 64.0f / 255.0f,
         "native PCM volume converts to normalized gain");

    levels.set(3, 128, 1000);
    near(levels.gain(3), 64.0f / 255.0f,
         "time-based volume fade starts from current gain");
    require(levels.fading(3), "fade duration is tracked per channel");
    std::vector<Event> events;
    const auto collect = [&](int channel, float gain, bool stop) {
      events.push_back({channel, gain, stop});
    };
    levels.advance(500, collect);
    near(levels.gain(3), 96.0f / 255.0f,
         "fade reaches exact interpolated midpoint");
    require(events.size() == 1 && events[0].channel == 3 &&
                !events[0].stop,
            "gain interpolation affects only the requested PCM channel");
    levels.advance(500, collect);
    require(levels.volume(3) == 128 && !levels.fading(3),
            "completed fade settles at target volume");
    const auto count = events.size();
    levels.advance(500, collect);
    require(events.size() == count,
            "settled fades must not reapply the final gain every frame");

    levels.set(0, 255);
    levels.set(5, 0, 250, true);
    events.clear();
    levels.advance(100, collect);
    require(levels.fading(5) && !events.back().stop,
            "fade-out cannot stop sound before duration elapses");
    levels.advance(150, collect);
    require(levels.volume(5) == 0 && events.back().stop,
            "fade-to-silence signals stop only at completion");
    const auto completedCount = events.size();
    levels.advance(200, collect);
    require(events.size() == completedCount,
            "stop request is emitted only once");

    levels.set(7, 0, 2000);
    levels.advance(500, collect);
    near(levels.gain(7), 0.75f, "independent channel fade starts");
    // During story pause update() doesn't advance the fade clock.
    near(levels.gain(7), 0.75f,
         "paused script time preserves current gain");
    levels.set(7, 255, 2000);
    levels.advance(UINT64_MAX, collect);
    near(levels.gain(7), 1.0f,
         "very large frame delta saturates instead of overflowing");

    levels.clear();
    require(levels.volume(3) == 255 && levels.volume(5) == 255 &&
                levels.volume(7) == 255,
            "title/load reset removes stale channel volume state");
    std::cout << "RealLive channel volume and fade regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RealLive channel regression: " << error.what() << '\n';
    return 1;
  }
}
