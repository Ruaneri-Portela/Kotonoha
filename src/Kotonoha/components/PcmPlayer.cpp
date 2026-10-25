#include <Kotonoha/components/PcmPlayer.hpp>
#include <Kotonoha/utils/Log.h>

#include <algorithm>
#include <atomic>
#include <climits>

namespace Kotonoha {

namespace {
constexpr int kChunkFrames = 1024; // per pipe render, ~21 ms at 48 kHz
constexpr size_t kFeedFrames = 2048;
} // namespace

struct PcmPlayer::Voice {
  std::vector<int16_t> samples;
  int channels = 1;
  int rate = 0;
  bool loop = false;
  size_t loopStart = 0;
  size_t frames = 0;
  size_t pos = 0; // next source frame fed to the converter (audio thread)
  bool drained = false;
  SDL_AudioStream *conv = nullptr;
  int outFrameBytes = 0;
  int outRate = 0;
  std::atomic<bool> done{false};
  std::atomic<float> ratio{1.0f};
  std::atomic<double> consumed{0.0}; // source frames handed to the mixer
};

static int RenderVoice(void *parms, Uint8 **target, int *size) {
  auto *v = static_cast<PcmPlayer::Voice *>(parms);
  *target = nullptr;
  *size = 0;
  if (v == nullptr || v->done.load()) {
    return 0;
  }
  const int want = kChunkFrames * v->outFrameBytes;
  while (!v->drained && SDL_GetAudioStreamAvailable(v->conv) < want) {
    if (v->pos >= v->frames) {
      if (v->loop) {
        v->pos = v->loopStart < v->frames ? v->loopStart : 0;
      } else {
        // Without a flush the resampler keeps its last few frames back.
        SDL_FlushAudioStream(v->conv);
        v->drained = true;
        break;
      }
    }
    const size_t n = std::min(kFeedFrames, v->frames - v->pos);
    SDL_PutAudioStreamData(v->conv, v->samples.data() + v->pos * size_t(v->channels),
                           int(n * size_t(v->channels) * sizeof(int16_t)));
    v->pos += n;
  }
  const int bytes = std::min(want, SDL_GetAudioStreamAvailable(v->conv)) / v->outFrameBytes * v->outFrameBytes;
  Uint8 *buffer = bytes > 0 ? static_cast<Uint8 *>(SDL_malloc(size_t(bytes))) : nullptr;
  const int got = buffer != nullptr ? SDL_GetAudioStreamData(v->conv, buffer, bytes) : 0;
  if (got <= 0) {
    SDL_free(buffer);
    if (v->drained) {
      v->done = true;
    }
    return 0;
  }
  const double frames = double(got / v->outFrameBytes);
  v->consumed.store(v->consumed.load() + frames * v->rate * v->ratio.load() / v->outRate);
  *target = buffer;
  *size = got;
  return got;
}

static void CloseVoice(void *parms) {
  auto *v = static_cast<PcmPlayer::Voice *>(parms);
  if (v == nullptr) {
    return;
  }
  if (v->conv != nullptr) {
    SDL_DestroyAudioStream(v->conv);
  }
  delete v;
}

PcmPlayer::~PcmPlayer() { Stop(); }

bool PcmPlayer::Play(Sound *sound, const char *channel, Clip clip) {
  Stop();
  Sound::Channel *ch = sound != nullptr && channel != nullptr ? sound->GetChannelByName(channel) : nullptr;
  if (ch == nullptr) {
    KLOG_WARN(AUDIO, "PCM clip: no audio channel '%s'", channel != nullptr ? channel : "");
    return false;
  }
  return Play(ch, std::move(clip));
}

bool PcmPlayer::Play(Sound::Channel *channel, Clip clip) {
  Stop();
  if (channel == nullptr || clip.channels <= 0 || clip.rate <= 0 ||
      clip.samples.size() < size_t(clip.channels)) {
    return false;
  }
  const SDL_AudioSpec in{SDL_AUDIO_S16, clip.channels, clip.rate};
  const SDL_AudioSpec out = channel->GetSpecs();
  auto *v = new Voice();
  v->conv = SDL_CreateAudioStream(&in, &out);
  if (v->conv == nullptr) {
    KLOG_ERROR(AUDIO, "PCM clip converter: %s", SDL_GetError());
    delete v;
    return false;
  }
  if (clip.gain != 1.0f) {
    SDL_SetAudioStreamGain(v->conv, clip.gain);
  }
  v->channels = clip.channels;
  v->rate = clip.rate;
  v->loop = clip.loop;
  v->loopStart = clip.loopStart;
  v->frames = clip.samples.size() / size_t(clip.channels);
  v->samples = std::move(clip.samples);
  v->pos = std::min(clip.startFrame, v->frames);
  v->consumed = double(v->pos);
  v->outFrameBytes = SDL_AUDIO_FRAMESIZE(out);
  v->outRate = out.freq;
  if (v->outFrameBytes <= 0 || v->outRate <= 0) {
    CloseVoice(v);
    return false;
  }
  // AddPipe renders the first chunk right away and frees the voice on failure.
  pipe_ = channel->AddPipe(RenderVoice, CloseVoice, v);
  if (pipe_ == nullptr) {
    return false;
  }
  channel_ = channel;
  voice_ = v;
  return true;
}

void PcmPlayer::Stop() {
  if (pipe_ != nullptr && channel_ != nullptr) {
    channel_->RemovePipe(pipe_);
  }
  pipe_ = nullptr;
  channel_ = nullptr;
  voice_ = nullptr;
}

bool PcmPlayer::Playing() const { return voice_ != nullptr && !voice_->done.load(); }

double PcmPlayer::Position() const {
  return voice_ != nullptr ? voice_->consumed.load() / double(voice_->rate) : 0.0;
}

int PcmPlayer::RemainingMs() const {
  if (!Playing()) {
    return 0;
  }
  if (voice_->loop) {
    return INT_MAX;
  }
  const double left = std::max(0.0, double(voice_->frames) - voice_->consumed.load());
  return int(left * 1000.0 / (double(voice_->rate) * voice_->ratio.load()));
}

void PcmPlayer::SetRate(int rate) {
  if (voice_ == nullptr || rate <= 0) {
    return;
  }
  const float ratio = float(rate) / float(voice_->rate);
  voice_->ratio = ratio;
  SDL_SetAudioStreamFrequencyRatio(voice_->conv, ratio);
}

void PcmPlayer::SetGain(float gain) {
  if (voice_ != nullptr) {
    SDL_SetAudioStreamGain(voice_->conv, std::max(0.0f, gain));
  }
}

int PcmPlayer::Rate() const {
  return voice_ != nullptr ? int(float(voice_->rate) * voice_->ratio.load() + 0.5f) : 0;
}

} // namespace Kotonoha
