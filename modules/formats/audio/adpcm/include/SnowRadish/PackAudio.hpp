#pragma once
// Audio format decoders for the Overflow ADV3 engine.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SnowRadish {

struct Pcm {
  std::vector<int16_t> samples; // interleaved when channels == 2
  int channels = 2;
};

// Decodes a raw ADPS block body (two interleaved 4-bit step-table channels).
Pcm decodeAdps(const uint8_t *data, size_t size);

// An .ADPS file (SE/BGM): [u32 introSize][u32 loopSize], then the intro block
// {u32 n, n bytes} and the loop block {u32 n, n bytes}. Played once the intro
// is followed by the loop block; when looping, only the loop block repeats.
struct AdpsTrack {
  Pcm intro, loop;
};
bool decodeAdpsFile(const std::vector<uint8_t> &data, AdpsTrack &out);

// Decodes an .ADP4 voice: u32 unit count, then flag-driven runs (one flag byte
// per 8 runs; a run is a 1- or 2-byte length) of either silence or ADPCM bytes
// XORed with an incrementing key. Each unit is two mono samples.
Pcm decodeAdp4(const std::vector<uint8_t> &data);

} // namespace SnowRadish
