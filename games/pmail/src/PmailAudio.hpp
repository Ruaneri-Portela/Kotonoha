#pragma once

#include <Kotonoha/media/PcmPlayer.hpp>
#include <SnowRadish/SnowAssets.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Kotonoha::PmailPort {

// Pure Mail's three script audio buses. SE remains SE even for a VOICE.DAT
// resource; this is required by SYS 101's wait and the independent voice bus.
class PmailAudio {
public:
  struct TrackState {
    std::string id;
    bool loop = false;
    bool playing = false;
    double seconds = 0;
    float gain = 1;
    float fadeTarget = 1;
    double fadeSeconds = 0;
  };
  using State =
      std::array<TrackState, 4>; // BGM, SE, voice, outgoing SE crossfade

  PmailAudio(Kotonoha::AudioMixer &mixer,
             std::shared_ptr<Kotonoha::Providers::SnowAssets> assets);
  ~PmailAudio();
  // Avoid decoding VM string references for unrelated/unknown native calls.
  // Keep this list synchronized with the cases handled by Handle().
  static constexpr bool HandlesSyscall(int id) {
    switch (id) {
    case 96:
    case 97:
    case 98:
    case 99:
    case 100:
    case 112:
    case 115:
    case 116:
    case 128:
    case 132:
    case 137:
      return true;
    default:
      return false;
    }
  }

  // pmail.exe:0x4248f0 expands SYS 137(category, day) into the retail sound
  // name V<base>5<variant><base>00<action>. Days 613..652 select actions
  // 0..39 and later days use action 40; category 12 synthesizes no name.
  static std::string DaySoundName(int category, int day);

  bool Handle(int syscall, const std::vector<int> &numbers,
              const std::vector<std::string> &strings);
  void Update();
  void StopAll();
  void SetPaused(bool paused);
  bool SePlaying() const;
  bool VoicePlaying() const;
  int VoiceMouthFrame() const;
  State Snapshot() const;
  void Restore(const State &state);
  static unsigned FadeInterval(int mode);

private:
  struct Slot {
    Kotonoha::PcmPlayer player;
    TrackState state;
    float fadeFrom = 1;
    uint64_t fadeStart = 0;
    uint64_t fadeDuration = 0;
  };
  Kotonoha::AudioMixer &mixer_;
  std::shared_ptr<Kotonoha::Providers::SnowAssets> assets_;
  std::array<std::unique_ptr<Slot>, 3> slots_;
  std::unique_ptr<Slot> outgoingSe_;
  bool paused_ = false;
  uint64_t pausedAt_ = 0;
  std::vector<uint8_t> voiceMouthFrames_;

  Kotonoha::PcmPlayer::Clip Load(const std::string &id, bool loop) const;
  void Play(size_t bus, const std::string &id, bool loop, float gain = 1,
            double seconds = 0);
  void Stop(Slot &slot);
  void Fade(Slot &slot, float target, double seconds);
  void Tick(Slot &slot, uint64_t now);
  TrackState Capture(const Slot &slot) const;
};
} // namespace Kotonoha::PmailPort
