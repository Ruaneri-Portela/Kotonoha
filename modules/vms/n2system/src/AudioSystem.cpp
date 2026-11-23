// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/AudioSystem.hpp>
#include <algorithm>

namespace N2System {

void AudioChannel::update(float dtSeconds) {
  if (isFading && fadeDuration > 0.0f) {
    fadeElapsed += dtSeconds;
    float t = std::min(1.0f, fadeElapsed / fadeDuration);
    // Interpolate from the captured start value; accumulating into the
    // already-modified volume produced a non-linear ramp and a jump at t=1.
    volume = fadeFrom + (targetVolume - fadeFrom) * t;
    if (t >= 1.0f) {
      volume = targetVolume;
      isFading = false;
      if (volume <= 0.001f) {
        isPlaying = false;
        assetPath.clear();
      }
    }
  }
}

AudioSystem::AudioSystem() {
  bgmChannel_.volume = 1.0f;
  voiceChannel_.volume = 1.0f;
}

void AudioSystem::playBgm(std::string_view path, bool loop, float fadeMs) {
  bgmChannel_.assetPath = std::string(path);
  bgmChannel_.isPlaying = true;
  bgmChannel_.loop = loop;

  if (fadeMs <= 0.0f) {
    bgmChannel_.volume = masterBgmVolume_;
    bgmChannel_.isFading = false;
  } else {
    bgmChannel_.fadeFrom = bgmChannel_.volume;
    bgmChannel_.volume = 0.0f;
    bgmChannel_.targetVolume = masterBgmVolume_;
    bgmChannel_.fadeDuration = fadeMs / 1000.0f;
    bgmChannel_.fadeElapsed = 0.0f;
    bgmChannel_.isFading = true;
  }
}

void AudioSystem::stopBgm(float fadeMs) {
  if (!bgmChannel_.isPlaying)
    return;

  if (fadeMs <= 0.0f) {
    bgmChannel_.isPlaying = false;
    bgmChannel_.assetPath.clear();
    bgmChannel_.volume = 0.0f;
    bgmChannel_.isFading = false;
  } else {
    bgmChannel_.fadeFrom = bgmChannel_.volume;
    bgmChannel_.targetVolume = 0.0f;
    bgmChannel_.fadeDuration = fadeMs / 1000.0f;
    bgmChannel_.fadeElapsed = 0.0f;
    bgmChannel_.isFading = true;
  }
}

void AudioSystem::playVoice(std::string_view path) {
  voiceChannel_.assetPath = std::string(path);
  voiceChannel_.isPlaying = true;
  voiceChannel_.loop = false;
  voiceChannel_.volume = masterVoiceVolume_;
  voiceChannel_.isFading = false;
}

void AudioSystem::stopVoice() {
  voiceChannel_.isPlaying = false;
  voiceChannel_.assetPath.clear();
}

void AudioSystem::playSe(std::string_view path, int channelIdx, bool loop) {
  auto &channel = seChannels_[channelIdx];
  channel.assetPath = std::string(path);
  channel.isPlaying = true;
  channel.loop = loop;
  channel.volume = masterSeVolume_;
  channel.isFading = false;
}

void AudioSystem::stopSe(int channelIdx) {
  auto it = seChannels_.find(channelIdx);
  if (it != seChannels_.end()) {
    it->second.isPlaying = false;
    it->second.assetPath.clear();
  }
}

void AudioSystem::setVolume(AudioChannelKind kind, float volume) {
  volume = std::max(0.0f, std::min(1.0f, volume));
  switch (kind) {
  case AudioChannelKind::Bgm:
    masterBgmVolume_ = volume;
    if (!bgmChannel_.isFading)
      bgmChannel_.volume = volume;
    break;
  case AudioChannelKind::Voice:
    masterVoiceVolume_ = volume;
    if (!voiceChannel_.isFading)
      voiceChannel_.volume = volume;
    break;
  case AudioChannelKind::Se:
    masterSeVolume_ = volume;
    for (auto &[idx, ch] : seChannels_) {
      if (!ch.isFading)
        ch.volume = volume;
    }
    break;
  }
}

void AudioSystem::fadeVolume(AudioChannelKind kind, float volume,
                             float fadeMs) {
  volume = std::max(0.0f, std::min(1.0f, volume));
  auto begin = [&](AudioChannel &channel, float &master) {
    master = volume;
    if (fadeMs <= 0.0f) {
      channel.volume = volume;
      channel.isFading = false;
      return;
    }
    channel.fadeFrom = channel.volume;
    channel.targetVolume = volume;
    channel.fadeDuration = fadeMs / 1000.0f;
    channel.fadeElapsed = 0.0f;
    channel.isFading = true;
    // A fade to silence must not stop a channel that is still playing.
    channel.isPlaying = true;
  };
  switch (kind) {
  case AudioChannelKind::Bgm:
    begin(bgmChannel_, masterBgmVolume_);
    break;
  case AudioChannelKind::Voice:
    begin(voiceChannel_, masterVoiceVolume_);
    break;
  case AudioChannelKind::Se:
    for (auto &[idx, ch] : seChannels_) {
      (void)idx;
      begin(ch, masterSeVolume_);
    }
    break;
  }
}

float AudioSystem::getVolume(AudioChannelKind kind) const {
  switch (kind) {
  case AudioChannelKind::Bgm:
    return masterBgmVolume_;
  case AudioChannelKind::Voice:
    return masterVoiceVolume_;
  case AudioChannelKind::Se:
    return masterSeVolume_;
  }
  return 1.0f;
}

void AudioSystem::restore(const AudioChannel &bgm, const AudioChannel &voice,
                          const std::unordered_map<int, AudioChannel> &se) {
  bgmChannel_ = bgm;
  voiceChannel_ = voice;
  seChannels_ = se;
}

void AudioSystem::update(float dtSeconds) {
  bgmChannel_.update(dtSeconds);
  voiceChannel_.update(dtSeconds);
  for (auto &[idx, ch] : seChannels_) {
    ch.update(dtSeconds);
  }
}

} // namespace N2System
