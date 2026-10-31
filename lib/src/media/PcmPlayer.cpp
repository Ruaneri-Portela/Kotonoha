#include <Kotonoha/media/PcmPlayer.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace Kotonoha {

void SDLCALL PcmPlayer::Feed(void *user, SDL_AudioStream *stream, int additional, int) {
  auto *v = static_cast<Voice *>(user);
  const Clip &clip = v->clip;
  const int frameBytes = clip.channels * int(sizeof(int16_t));
  size_t want = size_t(std::max(additional, 0) + frameBytes - 1) / size_t(frameBytes);
  size_t cursor = v->cursor.load();
  while (want > 0) {
    if (cursor >= v->frames) {
      if (!clip.loop || clip.loopStart >= v->frames) {
        if (!v->ended.exchange(true)) SDL_FlushAudioStream(stream);
        break;
      }
      cursor = clip.loopStart;
    }
    const size_t n = std::min(want, v->frames - cursor);
    if (!SDL_PutAudioStreamData(stream, clip.samples.data() + cursor * size_t(clip.channels), int(n) * frameBytes))
      break;
    cursor += n;
    want -= n;
    v->fed += n;
  }
  v->cursor = cursor;
}

bool PcmPlayer::Play(std::shared_ptr<AudioChannel> channel, Clip clip) {
  Stop();
  if (clip.channels <= 0 || clip.rate <= 0 || clip.samples.size() < size_t(clip.channels)) return false;
  if (clip.gain != 1.0f) {
    const float gain = std::clamp(clip.gain, 0.0f, 4.0f);
    for (int16_t &s : clip.samples)
      s = int16_t(std::clamp(std::lround(float(s) * gain), long(INT16_MIN), long(INT16_MAX)));
    clip.gain = 1.0f;
  }
  auto voice = std::make_unique<Voice>();
  voice->frames = clip.samples.size() / size_t(clip.channels);
  voice->cursor = std::min(clip.startFrame, voice->frames);
  const SDL_AudioSpec spec{SDL_AUDIO_S16, clip.channels, clip.rate};
  voice->clip = std::move(clip);
  SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, voice.get());
  if (!stream) {
    SDL_Log("[audio] cannot open stream: %s", SDL_GetError());
    return false;
  }
  try {
    if (channel) channel->attach(stream);
  } catch (...) {
    SDL_DestroyAudioStream(stream);
    return false;
  }
  stream_ = stream;
  channel_ = std::move(channel);
  voice_ = std::move(voice);
  rate_ = spec.freq;
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void PcmPlayer::Stop() {
  if (stream_) {
    if (channel_) channel_->detach(stream_);
    SDL_DestroyAudioStream(stream_);
  }
  stream_ = nullptr;
  channel_.reset();
  voice_.reset();
  rate_ = 0;
}

uint64_t PcmPlayer::Consumed() const {
  if (!stream_ || !voice_) return 0;
  SDL_LockAudioStream(stream_);
  const uint64_t fed = voice_->fed.load();
  const int queued = SDL_GetAudioStreamQueued(stream_);
  SDL_UnlockAudioStream(stream_);
  const uint64_t pending = uint64_t(std::max(queued, 0)) / uint64_t(voice_->clip.channels * 2);
  return fed > pending ? fed - pending : 0;
}

bool PcmPlayer::Playing() const {
  if (!stream_ || !voice_) return false;
  if (voice_->clip.loop) return true;
  return !voice_->ended.load() || SDL_GetAudioStreamQueued(stream_) > 0;
}

double PcmPlayer::Position() const {
  if (!voice_) return 0.0;
  return double(Consumed() + voice_->clip.startFrame) / double(voice_->clip.rate);
}

int PcmPlayer::RemainingMs() const {
  if (!Playing()) return 0;
  if (voice_->clip.loop) return INT_MAX;
  const uint64_t done = std::min<uint64_t>(Consumed() + voice_->clip.startFrame, voice_->frames);
  return int((voice_->frames - done) * 1000 / uint64_t(std::max(rate_, 1)));
}

void PcmPlayer::SetRate(int rate) {
  if (!stream_ || !voice_ || rate <= 0) return;
  if (SDL_SetAudioStreamFrequencyRatio(stream_, float(rate) / float(voice_->clip.rate))) rate_ = rate;
}

void PcmPlayer::SetGain(float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1)
    throw std::invalid_argument("PCM gain must be finite and between 0 and 1");
  if (!stream_) return;
  if (channel_) channel_->setStreamGain(stream_, gain);
  else if (!SDL_SetAudioStreamGain(stream_, gain))
    throw std::runtime_error("Cannot set PCM gain: " + std::string(SDL_GetError()));
}

} // namespace Kotonoha
