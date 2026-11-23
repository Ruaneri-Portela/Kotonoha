// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace N2System {

enum class AudioChannelKind {
  Bgm,
  Voice,
  Se
};

struct AudioChannel {
  std::string assetPath;
  bool isPlaying = false;
  bool loop = false;
  float volume = 1.0f;

  float fadeFrom = 1.0f;
  float targetVolume = 1.0f;
  float fadeDuration = 0.0f;
  float fadeElapsed = 0.0f;
  bool isFading = false;

  void update(float dtSeconds);
};

class AudioSystem {
public:
  AudioSystem();

  void playBgm(std::string_view path, bool loop = true, float fadeMs = 0.0f);
  void stopBgm(float fadeMs = 0.0f);
  // SetLoop() opcodes retarget the currently playing BGM without restarting
  // it, so the runtime can keep streaming the same asset across the change.
  void setBgmLoop(bool loop) { bgmChannel_.loop = loop; }

  void playVoice(std::string_view path);
  void stopVoice();

  void playSe(std::string_view path, int channelIdx = 0, bool loop = false);
  void stopSe(int channelIdx = 0);

  void setVolume(AudioChannelKind kind, float volume);
  // Native SetVolume carries a millisecond fade; use it instead of an
  // instantaneous jump so crossfades match the original engine.
  void fadeVolume(AudioChannelKind kind, float volume, float fadeMs);
  float getVolume(AudioChannelKind kind) const;

  const AudioChannel &bgmChannel() const { return bgmChannel_; }
  const AudioChannel &voiceChannel() const { return voiceChannel_; }
  const std::unordered_map<int, AudioChannel> &seChannels() const { return seChannels_; }

  void update(float dtSeconds);
  void restore(const AudioChannel &bgm, const AudioChannel &voice,
               const std::unordered_map<int, AudioChannel> &se);

private:
  AudioChannel bgmChannel_;
  AudioChannel voiceChannel_;
  std::unordered_map<int, AudioChannel> seChannels_;

  float masterBgmVolume_ = 1.0f;
  float masterVoiceVolume_ = 1.0f;
  float masterSeVolume_ = 1.0f;
};

} // namespace N2System
