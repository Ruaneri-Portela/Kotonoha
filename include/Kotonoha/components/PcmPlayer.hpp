#pragma once
#include <Kotonoha/components/Sound.hpp>

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace Kotonoha {

// Plays a decoded clip (interleaved signed 16-bit PCM, any rate and channel
// count) as a pipe of a Sound channel, so the channel and master volumes of
// the mixer apply to it. Starting a new clip replaces the current one.
class PcmPlayer {
public:
  struct Clip {
    std::vector<int16_t> samples; // interleaved
    int channels = 1;
    int rate = 0;
    bool loop = false;
    size_t loopStart = 0; // frame the loop restarts from
    float gain = 1.0f;
    size_t startFrame = 0; // frame playback begins at (the loop still restarts at loopStart)
  };

  PcmPlayer() = default;
  ~PcmPlayer();
  PcmPlayer(const PcmPlayer &) = delete;
  PcmPlayer &operator=(const PcmPlayer &) = delete;

  bool Play(Sound::Channel *channel, Clip clip);
  // Convenience: the channel by name ("BGM", "Se", "Voice") of a Sound.
  bool Play(Sound *sound, const char *channel, Clip clip);
  void Stop();

  // False once a one-shot clip has been mixed completely.
  bool Playing() const;
  bool Active() const { return pipe_ != nullptr; }
  // Clip time already mixed and what is left (both at the current rate).
  double Position() const;
  int RemainingMs() const;
  // Plays the clip at another sample rate (pitch and speed change together).
  void SetRate(int rate);
  int Rate() const;
  // Volume of this clip (fades); 1 = unchanged.
  void SetGain(float gain);

  struct Voice;

private:
  Sound::Channel *channel_ = nullptr;
  Sound::Channel::Pipe *pipe_ = nullptr;
  Voice *voice_ = nullptr;
};

} // namespace Kotonoha
