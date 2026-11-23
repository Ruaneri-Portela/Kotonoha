// SPDX-License-Identifier: MIT
#pragma once

namespace RealLive {

// RealLive's koeWait / wavWait instructions suspend script execution until
// the requested sound finishes. This is a cooperative VM barrier, not a
// guessed wall-clock timeout; pause/auto/skip can safely coexist with it.
class AudioWait {
public:
  enum class Kind { None, Voice, Pcm };
private:
  Kind kind_ = Kind::None;
  int pcmChannel_ = 0;
public:
  void clear() noexcept { kind_ = Kind::None; }
  bool active() const noexcept { return kind_ != Kind::None; }
  Kind kind() const noexcept { return kind_; }
  int channel() const noexcept { return pcmChannel_; }
  void forVoice(bool playing) noexcept {
    kind_ = playing ? Kind::Voice : Kind::None;
  }
  void forPcm(int channel, bool playing) noexcept {
    kind_ = playing ? Kind::Pcm : Kind::None;
    pcmChannel_ = channel;
  }

  template <class IsPcmPlaying>
  bool blocks(bool voicePlaying, IsPcmPlaying isPcmPlaying) {
    bool playing = false;
    switch (kind_) {
    case Kind::Voice:
      playing = voicePlaying;
      break;
    case Kind::Pcm:
      playing = isPcmPlaying(pcmChannel_);
      break;
    case Kind::None:
      return false;
    }
    if (!playing)
      clear();
    return playing;
  }
};

} // namespace RealLive
