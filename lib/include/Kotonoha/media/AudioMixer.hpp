#pragma once

#include <SDL3/SDL.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha {

class MediaPlayer;

// Control API belongs to the engine thread; SDL applies gain during mixing.
class AudioChannel {
  struct Stream {
    SDL_AudioStream *stream;
    float gain;
  };
  std::string name;
  float volume = 1;
  bool muted = false;
  float masterGain = 1;
  std::vector<Stream> streams;
  void apply(float value, bool mute, float master);
  explicit AudioChannel(std::string name, float master);
  friend class AudioMixer;
  friend class MediaPlayer;

public:
  const std::string &getName() const { return name; }
  float getVolume() const { return volume; }
  bool isMuted() const { return muted; }
  std::size_t activeStreams() const { return streams.size(); }
  void setVolume(float value);
  void setMuted(bool value);
  // Streams a game plays itself (e.g. a custom PCM player) join the channel
  // here; detach before destroying the stream.
  void attach(SDL_AudioStream *stream);
  void detach(SDL_AudioStream *stream) noexcept;
  // Per-stream gain composes with the channel volume and mute.
  void setStreamGain(SDL_AudioStream *stream, float gain);
};

class AudioMixer {
  std::map<std::string, std::shared_ptr<AudioChannel>> channels;
  float masterVolume = 1;
  bool masterMuted = false;
  void applyMaster(float volume, bool muted);

public:
  AudioMixer();
  float getMasterVolume() const { return masterVolume; }
  bool isMasterMuted() const { return masterMuted; }
  void setMasterVolume(float value);
  void setMasterMuted(bool value);
  static void validateChannelName(const std::string &name);
  std::shared_ptr<AudioChannel> channel(const std::string &name);
  std::vector<std::shared_ptr<AudioChannel>> getChannels() const;
};

} // namespace Kotonoha
