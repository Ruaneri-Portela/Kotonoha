#include "PmailAudio.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace Kotonoha::PmailPort {
namespace {
constexpr const char *channels[] = {"bgm", "se", "voice"};
}

PmailAudio::PmailAudio(Kotonoha::AudioMixer &mixer,
                     std::shared_ptr<Kotonoha::Providers::SnowAssets> assets)
    : mixer_(mixer), assets_(std::move(assets)), outgoingSe_(std::make_unique<Slot>()) {
  if (!assets_) throw std::invalid_argument("Pure Mail audio needs its asset provider");
  for (auto &slot : slots_) slot = std::make_unique<Slot>();
}
PmailAudio::~PmailAudio() { StopAll(); }

unsigned PmailAudio::FadeInterval(int mode) {
  // pmail.exe 0x404fa0 sets the base interval to 43; 0x404fc0 scales it.
  switch (mode) {
  case 1: return 43 / 2;
  case 2: return 43 * 2;
  case 3: return 43 / 4;
  case 4: return 43 * 4;
  default: return 43;
  }
}

Kotonoha::PcmPlayer::Clip PmailAudio::Load(const std::string &id, bool loop) const {
  Kotonoha::PcmPlayer::Clip result;
  // Keep the separately authored introduction/loop boundary of retail ADPS.
  if (id.rfind("sound/mm", 0) == 0 && !assets_->hasOverlay(id)) {
    SnowRadish::AdpsTrack track;
    if (!assets_->Adps(id.substr(6), track))
      throw std::runtime_error("Cannot decode Pure Mail music: " + id);
    result.channels = 2;
    result.rate = assets_->sampleRate();
    result.loopStart = track.intro.samples.size() / 2;
    result.samples = std::move(track.intro.samples);
    result.samples.insert(result.samples.end(), track.loop.samples.begin(), track.loop.samples.end());
    result.loop = loop && !track.loop.samples.empty();
    return result;
  }
  const auto audio = assets_->loadAudio(id);
  result.channels = audio.spec.channels;
  result.rate = audio.spec.freq;
  result.loop = loop;
  if (audio.data.empty()) return result; // zero-unit retail voice records
  Uint8 *converted = nullptr;
  int bytes = 0;
  SDL_AudioSpec target{SDL_AUDIO_S16, audio.spec.channels, audio.spec.freq};
  if (!SDL_ConvertAudioSamples(&audio.spec, audio.data.data(), int(audio.data.size()),
                               &target, &converted, &bytes))
    throw std::runtime_error(SDL_GetError());
  result.samples.resize(size_t(bytes) / sizeof(int16_t));
  if (bytes) std::memcpy(result.samples.data(), converted, size_t(bytes));
  SDL_free(converted);
  return result;
}

void PmailAudio::Stop(Slot &slot) {
  slot.player.Stop();
  slot.state = {};
  slot.fadeDuration = 0;
}

void PmailAudio::Play(size_t bus, const std::string &rawId, bool loop,
                      float gain, double seconds) {
  const auto id = Kotonoha::Providers::SnowAssets::Lower(rawId);
  auto clip = Load(id, loop); // Decode before replacing an existing stream.
  auto &slot = *slots_.at(bus);
  Stop(slot);
  const size_t frames = clip.channels > 0 ? clip.samples.size() / size_t(clip.channels) : 0;
  if (frames == 0) return;
  size_t start = size_t(std::max(0.0, seconds) * clip.rate);
  if (start >= frames && clip.loop && clip.loopStart < frames)
    start = clip.loopStart + (start - clip.loopStart) % (frames - clip.loopStart);
  if (start >= frames) return;
  clip.startFrame = start;
  clip.gain = std::clamp(gain, 0.0f, 1.0f);
  slot.state = {id, clip.loop, true, seconds, clip.gain, clip.gain, 0};
  if (!slot.player.Play(mixer_.channel(channels[bus]), std::move(clip)))
    throw std::runtime_error("Cannot start Pure Mail audio: " + id + ": " + SDL_GetError());
  slot.player.SetPaused(paused_);
}

void PmailAudio::Fade(Slot &slot, float target, double seconds) {
  if (!slot.player.Playing()) return;
  slot.fadeFrom = slot.state.gain;
  slot.state.fadeTarget = target;
  slot.state.fadeSeconds = seconds;
  slot.fadeStart = paused_ ? pausedAt_ : SDL_GetTicksNS();
  slot.fadeDuration = uint64_t(std::max(0.0, seconds) * 1e9);
}
void PmailAudio::Tick(Slot &slot, uint64_t now) {
  if (!slot.fadeDuration) return;
  const double progress = std::min(1.0, double(now - slot.fadeStart) / slot.fadeDuration);
  // Native mixer steps its gain through 0..256 (0x420d90).
  const float gain = std::floor((slot.fadeFrom + (slot.state.fadeTarget - slot.fadeFrom) * progress) * 256.0f) / 256.0f;
  slot.player.SetGain(std::clamp(gain, 0.0f, 1.0f));
  slot.state.gain = gain;
  if (progress >= 1) {
    slot.fadeDuration = 0;
    slot.state.fadeSeconds = 0;
    if (slot.state.fadeTarget == 0) Stop(slot);
  }
}
void PmailAudio::Update() {
  if (paused_) return;
  const auto now = SDL_GetTicksNS();
  for (auto &slot : slots_) Tick(*slot, now);
  Tick(*outgoingSe_, now);
}
void PmailAudio::StopAll() {
  for (auto &slot : slots_) Stop(*slot);
  Stop(*outgoingSe_);
}
void PmailAudio::SetPaused(bool paused) {
  if (paused == paused_) return;
  const auto now = SDL_GetTicksNS();
  if (paused) pausedAt_ = now;
  auto apply = [&](Slot &slot) {
    slot.player.SetPaused(paused);
    if (!paused && slot.fadeDuration) slot.fadeStart += now - pausedAt_;
  };
  for (auto &slot : slots_) apply(*slot);
  apply(*outgoingSe_);
  paused_ = paused;
}
bool PmailAudio::SePlaying() const { return slots_[1]->player.Playing() || outgoingSe_->player.Playing(); }
bool PmailAudio::VoicePlaying() const { return slots_[2]->player.Playing(); }

PmailAudio::TrackState PmailAudio::Capture(const Slot &slot) const {
  auto state = slot.state;
  state.playing = slot.player.Playing();
  state.seconds = slot.player.Position();
  if (slot.fadeDuration) {
    const auto elapsed = (paused_ ? pausedAt_ : SDL_GetTicksNS()) - slot.fadeStart;
    state.fadeSeconds = elapsed < slot.fadeDuration ? double(slot.fadeDuration - elapsed) / 1e9 : 0;
  }
  return state;
}
PmailAudio::State PmailAudio::Snapshot() const {
  State state;
  for (size_t i = 0; i < state.size(); ++i) state[i] = Capture(*slots_[i]);
  return state;
}
void PmailAudio::Restore(const State &state) {
  for (const auto &track : state)
    if (!std::isfinite(track.seconds) || track.seconds < 0 || !std::isfinite(track.gain) ||
        track.gain < 0 || track.gain > 1 || !std::isfinite(track.fadeSeconds) || track.fadeSeconds < 0 ||
        !std::isfinite(track.fadeTarget) || track.fadeTarget < 0 || track.fadeTarget > 1)
      throw std::runtime_error("Invalid Pure Mail saved audio state");
  StopAll();
  for (size_t i = 0; i < state.size(); ++i) {
    const auto &track = state[i];
    if (!track.playing || track.id.empty()) continue;
    Play(i, track.id, track.loop, track.gain, track.seconds);
    if (track.fadeSeconds > 0) Fade(*slots_[i], track.fadeTarget, track.fadeSeconds);
  }
}

bool PmailAudio::Handle(int syscall, const std::vector<int> &numbers,
                        const std::vector<std::string> &strings) {
  const auto number = [&](size_t i) { return i < numbers.size() ? numbers[i] : 0; };
  const auto name = strings.empty() ? std::string() : strings.front();
  const auto fadeSeconds = [&](int mode) { return FadeInterval(mode) * 256.0 / 44100.0; };
  switch (syscall) {
  case 96:
  case 97:
  case 98: {
    if (name.empty()) {
      if (syscall == 98) Fade(*slots_[1], 0, fadeSeconds(number(1)));
      else Stop(*slots_[1]);
      return true;
    }
    const auto stem = Kotonoha::Providers::SnowAssets::Lower(name);
    const std::string id = assets_->contains("voice/" + stem) ? "voice/" + stem : "sound/" + stem;
    if (syscall == 98) {
      Stop(*outgoingSe_);
      std::swap(slots_[1], outgoingSe_);
      Fade(*outgoingSe_, 0, fadeSeconds(number(1)));
    } else Stop(*outgoingSe_);
    Play(1, id, number(0) != 0, syscall == 96 ? 1 : 0);
    if (syscall != 96) Fade(*slots_[1], 1, fadeSeconds(number(1)));
    return true;
  }
  case 99: Stop(*slots_[1]); Stop(*outgoingSe_); return true;
  case 100: Fade(*slots_[1], 0, fadeSeconds(number(0))); return true;
  case 112: {
    char file[32];
    std::snprintf(file, sizeof(file), "sound/mm%03d", number(0));
    Play(0, file, number(1) != 0);
    return true;
  }
  case 115: Stop(*slots_[0]); return true;
  case 116: Fade(*slots_[0], 0, fadeSeconds(number(0))); return true;
  case 128:
    if (name.empty()) Stop(*slots_[2]);
    else Play(2, "voice/" + name, false);
    return true;
  case 132: Stop(*slots_[2]); return true;
  default: return false;
  }
}
} // namespace Kotonoha::PmailPort
