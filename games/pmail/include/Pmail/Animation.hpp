#pragma once

#include <array>
#include <cstdint>

namespace Kotonoha::PmailPort {

struct AnimationPreset {
  const char *name;
  int width, height;
  uint32_t frameMs;
  bool restore;
};

// pmail.exe SYS 51 (0x4240e0), jump table at 0x42443c. The coordinates
// are script arguments; the dimensions, interval and restore flag are native.
inline constexpr std::array<AnimationPreset, 19> AnimationPresets{{
    {"osboot", 320, 240, 54, false},  {"ikari", 67, 87, 125, true},
    {"jitoase", 27, 97, 125, true},   {"ase", 80, 131, 125, true},
    {"aser", 95, 120, 125, true},     {"mouse", 40, 64, 160, true},
    {"ok", 98, 28, 200, true},        {"error", 98, 28, 200, true},
    {"pass_o", 98, 28, 200, true},    {"pass_c", 98, 28, 200, true},
    {"pass0103", 180, 12, 180, true}, {"pass0104", 180, 12, 180, true},
    {"pass0105", 180, 12, 180, true}, {"pass0106", 180, 12, 180, true},
    {"pass0107", 180, 12, 180, true}, {"pass0108", 180, 12, 180, true},
    {"pass0109", 180, 12, 180, true}, {"pass0110", 180, 12, 180, true},
    {"pass0111", 180, 12, 180, true},
}};

struct AnimationState {
  uint32_t preset = 0;
  int x = 0, y = 0;
  uint32_t frame = 0, count = 0, elapsedMs = 0;
  bool playing = false;
};

// pmail.exe 0x41f58c..0x41f5c6. Silence closes the mouth without advancing
// the pattern. The two 16-entry patterns live at 0x44e9d4 and 0x44e9e4.
inline uint8_t MouthFrame(int sample, uint8_t &phase) {
  constexpr uint8_t quiet[]{1, 1, 2, 0, 1, 1, 0, 2, 1, 1, 1, 0, 2, 1, 1, 0};
  constexpr uint8_t loud[]{2, 0, 2, 1, 2, 1, 0, 2, 0, 1, 2, 1, 2, 0, 2, 1};
  if (sample >= -64 && sample <= 64)
    return 0;
  phase = (phase + 1) & 15;
  return sample < -16378 || sample > 16377 ? loud[phase] : quiet[phase];
}

} // namespace Kotonoha::PmailPort
