#include <Kotonoha/media/AudioMixer.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

#ifdef KOTONOHA_DEV_CONTROL
void SDLCALL AudioChannel::postmix(void *userdata, const SDL_AudioSpec *spec,
                                    float *buffer, int buflen) {
  auto *self = static_cast<AudioChannel *>(userdata);
  if (!self || !spec || !buffer || buflen <= 0) return;
  const int count = buflen / static_cast<int>(sizeof(float));
  double energy = 0.0, peak = 0.0;
  uint64_t clipped = 0;
  for (int i = 0; i < count; ++i) {
    const double value = std::isfinite(buffer[i]) ? std::abs(static_cast<double>(buffer[i])) : 1.0;
    peak = std::max(peak, value);
    energy += value * value;
    if (value >= 0.999999) ++clipped;
  }
  self->observedSamples_.fetch_add(static_cast<uint64_t>(count), std::memory_order_relaxed);
  self->clippedSamples_.fetch_add(clipped, std::memory_order_relaxed);
  if (peak == 0.0 && self->activeStreams_.load(std::memory_order_relaxed) > 0 &&
      self->audible_.load(std::memory_order_relaxed))
    self->silentBlocks_.fetch_add(1, std::memory_order_relaxed);
  self->sampleRate_.store(spec->freq, std::memory_order_relaxed);
  self->observedChannels_.store(spec->channels, std::memory_order_relaxed);
  double previous = self->energy_.load(std::memory_order_relaxed);
  while (!self->energy_.compare_exchange_weak(previous, previous + energy,
                                               std::memory_order_relaxed)) {}
  previous = self->peak_.load(std::memory_order_relaxed);
  while (previous < peak &&
         !self->peak_.compare_exchange_weak(previous, peak,
                                            std::memory_order_relaxed)) {}
}

AudioMetricsSnapshot AudioChannel::metrics() const {
  AudioMetricsSnapshot result;
  result.samples = observedSamples_.load(std::memory_order_relaxed);
  result.peak = peak_.load(std::memory_order_relaxed);
  result.rms = result.samples ?
      std::sqrt(energy_.load(std::memory_order_relaxed) / result.samples) : 0.0;
  result.clippedSamples = clippedSamples_.load(std::memory_order_relaxed);
  result.underrunCandidates = silentBlocks_.load(std::memory_order_relaxed);
  result.sampleRate = sampleRate_.load(std::memory_order_relaxed);
  result.channels = observedChannels_.load(std::memory_order_relaxed);
  return result;
}
#endif

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
    if (!SDL_SetAudioStreamGain(entry.stream, mute ? 0
                                                   : master * userVolume *
                                                         value * entry.gain)) {
      const std::string failure = SDL_GetError();
      for (std::size_t i = 0; i < changed; ++i)
        if (!SDL_SetAudioStreamGain(
                streams[i].stream,
                muted ? 0 : masterGain * userVolume * volume * streams[i].gain))
          throw std::runtime_error(
              failure + "; audio gain rollback failed: " + SDL_GetError());
      throw std::runtime_error("Cannot set audio channel gain: " + failure);
    }
    ++changed;
  }
  volume = value;
  muted = mute;
  masterGain = master;
#ifdef KOTONOHA_DEV_CONTROL
  audible_.store(!mute && master > 0.0f && value > 0.0f,
                 std::memory_order_relaxed);
#endif
}

void AudioChannel::setUserVolume(float value) {
  if (!std::isfinite(value) || value < 0 || value > 1)
    throw std::invalid_argument("Invalid user mixer volume");
  const auto old = userVolume;
  userVolume = value;
  try {
    apply(volume, muted, masterGain);
  } catch (...) {
    userVolume = old;
    apply(volume, muted, masterGain);
    throw;
  }
}

void AudioMixer::setUserVolumes(const std::map<std::string, float> &values) {
  for (const auto &[name, value] : values) {
    validateChannelName(name);
    if (!std::isfinite(value) || value < 0 || value > 1)
      throw std::invalid_argument("Invalid user mixer volume");
  }
  const auto old = userVolumes;
  try {
    for (auto &[name, channel] : channels) {
      auto found = values.find(name);
      channel->setUserVolume(found == values.end() ? 1 : found->second);
    }
    userVolumes = values;
  } catch (...) {
    for (auto &[name, channel] : channels) {
      auto found = old.find(name);
      channel->setUserVolume(found == old.end() ? 1 : found->second);
    }
    throw;
  }
}

void AudioChannel::setVolume(float value) { apply(value, muted, masterGain); }
void AudioChannel::setMuted(bool value) { apply(volume, value, masterGain); }

void AudioChannel::attach(SDL_AudioStream *stream, float sourceGain) {
  if (!std::isfinite(sourceGain) || sourceGain < 0 || sourceGain > 4)
    throw std::invalid_argument("Invalid audio source gain");
  if (!stream || std::any_of(streams.begin(), streams.end(),
                             [stream](const Stream &entry) {
                               return entry.stream == stream;
                             }))
    throw std::invalid_argument("Invalid or duplicate audio channel stream");
  if (!SDL_SetAudioStreamGain(stream, muted ? 0 : masterGain * volume * sourceGain))
    throw std::runtime_error(SDL_GetError());
#ifdef KOTONOHA_DEV_CONTROL
  const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream);
  if (device != 0 && !SDL_SetAudioPostmixCallback(device, postmix, this))
    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "DEV audio postmix unavailable: %s", SDL_GetError());
#endif
  streams.push_back({stream, sourceGain});
#ifdef KOTONOHA_DEV_CONTROL
  activeStreams_.store(static_cast<unsigned>(streams.size()), std::memory_order_relaxed);
#endif
}

void AudioChannel::detach(SDL_AudioStream *stream) noexcept {
#ifdef KOTONOHA_DEV_CONTROL
  if (stream) {
    const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream);
    if (device != 0) (void)SDL_SetAudioPostmixCallback(device, nullptr, nullptr);
  }
#endif
  streams.erase(std::remove_if(streams.begin(), streams.end(),
                               [stream](const Stream &entry) {
                                 return entry.stream == stream;
                               }),
                streams.end());
#ifdef KOTONOHA_DEV_CONTROL
  activeStreams_.store(static_cast<unsigned>(streams.size()), std::memory_order_relaxed);
#endif
}

void AudioChannel::setStreamGain(SDL_AudioStream *stream, float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw std::invalid_argument(
        "Audio stream gain must be finite and between 0 and 1");
  const auto found = std::find_if(
      streams.begin(), streams.end(),
      [stream](const Stream &entry) { return entry.stream == stream; });
  if (found == streams.end())
    throw std::invalid_argument("Audio stream is not attached to this channel");
  if (!SDL_SetAudioStreamGain(
          stream, muted ? 0 : masterGain * userVolume * volume * gain))
    throw std::runtime_error("Cannot set audio stream gain: " +
                             std::string(SDL_GetError()));
  found->gain = gain;
}

AudioMixer::AudioMixer() {
  channel("audio");
  channel("video");
}

void AudioMixer::applyMaster(float volume, bool muted) {
  if (!std::isfinite(volume) || volume < 0 || volume > 1)
    throw std::invalid_argument(
        "Master volume must be finite and between 0 and 1");
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
      channel.apply(channel.volume, channel.muted,
                    masterMuted ? 0 : masterVolume);
    }
    throw std::runtime_error("Cannot set master volume: " + failure);
  }
  masterVolume = volume;
  masterMuted = muted;
}

void AudioMixer::setMasterVolume(float value) {
  applyMaster(value, masterMuted);
}
void AudioMixer::setMasterMuted(bool value) {
  applyMaster(masterVolume, value);
}

std::shared_ptr<AudioChannel> AudioMixer::channel(const std::string &name) {
  validateChannelName(name);
  const auto found = channels.find(name);
  if (found != channels.end())
    return found->second;
  auto created = std::shared_ptr<AudioChannel>(
      new AudioChannel(name, masterMuted ? 0 : masterVolume));
  if (auto found = userVolumes.find(name); found != userVolumes.end())
    created->setUserVolume(found->second);
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
