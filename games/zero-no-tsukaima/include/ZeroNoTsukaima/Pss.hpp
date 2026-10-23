#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ZeroNoTsukaima {

// PS2 .PSS movie: an MPEG-2 program stream whose video is stream 0xE0 and whose
// audio sits in private stream 1 (0xBD): each payload starts with a 4-byte
// sub-stream header; the concatenated audio begins with "SShd" (u32 size, u32
// type (1 = s16le PCM), u32 rate, u32 channels, u32 interleave, loop points)
// and "SSbd" (u32 size), then `interleave`-byte blocks alternating channels.
struct PssMovie {
  struct Packet {
    std::vector<uint8_t> data; // MPEG-2 video elementary stream bytes
    int64_t pts = -1;          // 90 kHz, -1 when the PES carries none
  };
  std::vector<Packet> video;
  std::vector<int16_t> pcm; // interleaved
  int rate = 0, channels = 0;
};

bool DemuxPss(const uint8_t *data, size_t size, PssMovie *out);

} // namespace ZeroNoTsukaima
