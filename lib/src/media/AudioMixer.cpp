#include <Kotonoha/media/AudioMixer.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

AudioChannel::AudioChannel(std::string value, float master)
    : name(std::move(value)), masterGain(master) {}

void AudioMixer::validateChannelName(const std::string &name) {
  if (name.empty() || name.size() > 128 ||
      name.find_first_not_of(" \t\r\n") == std::string::npos ||
      name.find('\0') != std::string::npos ||
      std::any_of(name.begin(), name.end(),
                  [](unsigned char ch) { return ch < 32 || ch == 127; }))
    throw std::invalid_argument(
        "Audio channel name must contain 1..128 non-control bytes");
}

void AudioChannel::apply(float value, bool mute, float master) {
  if (!std::isfinite(value) || value < 0 || value > 1)
    throw std::invalid_argument(
        "Audio channel volume must be finite and between 0 and 1");
  std::size_t changed = 0;
  for (const auto &entry : streams) {
    if (!SDL_SetAudioStreamGain(entry.stream, mute ? 0 : master * value * entry.gain)) {
      const std::string failure = SDL_GetError();
      for (std::size_t i = 0; i < changed; ++i)
        if (!SDL_SetAudioStreamGain(streams[i].stream,
                                    muted ? 0 : masterGain * volume * streams[i].gain))
          throw std::runtime_error(
              failure + "; audio gain rollback failed: " + SDL_GetError());
      throw std::runtime_error("Cannot set audio channel gain: " + failure);
    }
    ++changed;
  }
  volume = value;
  muted = mute;
  masterGain = master;
}

void AudioChannel::setVolume(float value) { apply(value, muted, masterGain); }
void AudioChannel::setMuted(bool value) { apply(volume, value, masterGain); }

void AudioChannel::attach(SDL_AudioStream *stream) {
  if (!stream || std::any_of(streams.begin(), streams.end(),
                             [stream](const Stream &entry) {
                               return entry.stream == stream;
                             }))
    throw std::invalid_argument("Invalid or duplicate audio channel stream");
  if (!SDL_SetAudioStreamGain(stream, muted ? 0 : masterGain * volume))
    throw std::runtime_error(SDL_GetError());
  streams.push_back({stream, 1});
}

void AudioChannel::detach(SDL_AudioStream *stream) noexcept {
  streams.erase(std::remove_if(streams.begin(), streams.end(),
                               [stream](const Stream &entry) {
                                 return entry.stream == stream;
                               }),
                streams.end());
}

void AudioChannel::setStreamGain(SDL_AudioStream *stream, float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw std::invalid_argument("Audio stream gain must be finite and between 0 and 1");
  const auto found = std::find_if(streams.begin(), streams.end(),
                                  [stream](const Stream &entry) {
                                    return entry.stream == stream;
                                  });
  if (found == streams.end())
    throw std::invalid_argument("Audio stream is not attached to this channel");
  if (!SDL_SetAudioStreamGain(stream, muted ? 0 : masterGain * volume * gain))
    throw std::runtime_error("Cannot set audio stream gain: " + std::string(SDL_GetError()));
  found->gain = gain;
}

AudioMixer::AudioMixer() {
  channel("audio");
  channel("video");
}

void AudioMixer::applyMaster(float volume, bool muted) {
  if (!std::isfinite(volume) || volume < 0 || volume > 1)
    throw std::invalid_argument("Master volume must be finite and between 0 and 1");
  auto failed = channels.begin();
  try {
    for (; failed != channels.end(); ++failed) {
      auto &channel = *failed->second;
      channel.apply(channel.volume, channel.muted, muted ? 0 : volume);
    }
  } catch (const std::exception &error) {
    const std::string failure = error.what();
    for (auto it = channels.begin(); it != failed; ++it) {
      auto &channel = *it->second;
      channel.apply(channel.volume, channel.muted, masterMuted ? 0 : masterVolume);
    }
    throw std::runtime_error("Cannot set master volume: " + failure);
  }
  masterVolume = volume;
  masterMuted = muted;
}

void AudioMixer::setMasterVolume(float value) { applyMaster(value, masterMuted); }
void AudioMixer::setMasterMuted(bool value) { applyMaster(masterVolume, value); }

std::shared_ptr<AudioChannel> AudioMixer::channel(const std::string &name) {
  validateChannelName(name);
  const auto found = channels.find(name);
  if (found != channels.end())
    return found->second;
  auto created = std::shared_ptr<AudioChannel>(
      new AudioChannel(name, masterMuted ? 0 : masterVolume));
  channels.emplace(name, created);
  return created;
}

std::vector<std::shared_ptr<AudioChannel>> AudioMixer::getChannels() const {
  std::vector<std::shared_ptr<AudioChannel>> result;
  for (const auto &entry : channels)
    result.push_back(entry.second);
  return result;
}

} // namespace Kotonoha
