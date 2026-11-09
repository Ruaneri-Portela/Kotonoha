#pragma once

#include <SDL3/SDL.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha {

#ifdef KOTONOHA_DEV_CONTROL
struct AudioMetricsSnapshot {
  double peak = 0.0;
  double rms = 0.0;
  uint64_t samples = 0;
  uint64_t clippedSamples = 0;
  uint64_t underrunCandidates = 0;
  int sampleRate = 0;
  int channels = 0;
};
#endif

class MediaPlayer;

// Control API belongs to the engine thread; SDL applies gain during mixing.
class AudioChannel {
  struct Stream {
    SDL_AudioStream *stream;
    float gain;
  };
  std::string name;
  float volume = 1;
  float userVolume = 1;
  bool muted = false;
  float masterGain = 1;
  std::vector<Stream> streams;
#ifdef KOTONOHA_DEV_CONTROL
  std::atomic<uint64_t> observedSamples_{0}, clippedSamples_{0},
      silentBlocks_{0};
  std::atomic<double> energy_{0.0}, peak_{0.0};
  std::atomic<unsigned> activeStreams_{0};
  std::atomic<bool> audible_{true};
  std::atomic<int> sampleRate_{0}, observedChannels_{0};
  static void SDLCALL postmix(void *userdata, const SDL_AudioSpec *spec,
                              float *buffer, int buflen);
#endif
  void apply(float value, bool mute, float master);
  explicit AudioChannel(std::string name, float master);
  friend class AudioMixer;
  friend class MediaPlayer;

public:
  const std::string &getName() const { return name; }
  float getVolume() const { return volume; }
  float getUserVolume() const { return userVolume; }
  void setUserVolume(float value);
  bool isMuted() const { return muted; }
  std::size_t activeStreams() const { return streams.size(); }
#ifdef KOTONOHA_DEV_CONTROL
  AudioMetricsSnapshot metrics() const;
#endif
  void setVolume(float value);
  void setMuted(bool value);
  // Streams a game plays itself (e.g. a custom PCM player) join the channel
  // here; detach before destroying the stream.
  void attach(SDL_AudioStream *stream, float sourceGain = 1.0f);
  void detach(SDL_AudioStream *stream) noexcept;
  // Per-stream gain composes with the channel volume and mute.
  void setStreamGain(SDL_AudioStream *stream, float gain);
};

class AudioMixer {
  std::map<std::string, std::shared_ptr<AudioChannel>> channels;
  float masterVolume = 1;
  std::map<std::string, float> userVolumes;
  bool masterMuted = false;
  void applyMaster(float volume, bool muted);

public:
  AudioMixer();
  float getMasterVolume() const { return masterVolume; }
  bool isMasterMuted() const { return masterMuted; }
  void setMasterVolume(float value);
  void setUserVolumes(const std::map<std::string, float> &values);
  void setMasterMuted(bool value);
  static void validateChannelName(const std::string &name);
  std::shared_ptr<AudioChannel> channel(const std::string &name);
  std::vector<std::shared_ptr<AudioChannel>> getChannels() const;
};

} // namespace Kotonoha
