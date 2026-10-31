#pragma once

#include <Kotonoha/media/AudioMixer.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Kotonoha {

// Interleaved 16-bit PCM played on a named engine audio channel, so the
// channel volume / mute of the debug UI apply to it.
class PcmPlayer {
public:
  struct Clip {
    std::vector<int16_t> samples; // interleaved
    int channels = 1;
    int rate = 22050;
    bool loop = false;
    size_t loopStart = 0; // frame the loop restarts at
    float gain = 1.0f;
    size_t startFrame = 0;
  };

  PcmPlayer() = default;
  ~PcmPlayer() { Stop(); }
  PcmPlayer(const PcmPlayer &) = delete;
  PcmPlayer &operator=(const PcmPlayer &) = delete;

  bool Play(std::shared_ptr<AudioChannel> channel, Clip clip);
  void Stop();
  // Still producing sound (always while looping).
  bool Playing() const;
  bool Active() const { return stream_ != nullptr; }
  // Seconds of the clip already played.
  double Position() const;
  // Milliseconds left; INT_MAX while looping, 0 when stopped.
  int RemainingMs() const;
  // Playback rate in Hz (applied live as a frequency ratio).
  void SetRate(int rate);
  int Rate() const { return rate_; }
  // Per-player gain (0..1), multiplied by the named channel's volume.
  void SetGain(float gain);

private:
  struct Voice {
    Clip clip;
    size_t frames = 0;
    std::atomic<size_t> cursor{0}; // next frame to queue
    std::atomic<uint64_t> fed{0};  // frames queued so far
    std::atomic<bool> ended{false};
  };
  static void SDLCALL Feed(void *user, SDL_AudioStream *stream, int additional, int total);
  uint64_t Consumed() const;

  std::unique_ptr<Voice> voice_;
  std::shared_ptr<AudioChannel> channel_;
  SDL_AudioStream *stream_ = nullptr;
  int rate_ = 0;
};

} // namespace Kotonoha
