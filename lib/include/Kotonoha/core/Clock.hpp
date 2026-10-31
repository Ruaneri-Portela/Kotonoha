#pragma once
#include <cstdint>

namespace Kotonoha {

class Clock {
private:
  uint64_t anchor;
  double position = 0.0;
  bool paused = false;

public:
  Clock();

  void pause();
  void resume();

  void reset();

  void advance(double seconds);
  void rewind(double seconds);

  double seconds() const;
  double milliseconds() const;

  bool isPaused() const;
};

} // namespace Kotonoha