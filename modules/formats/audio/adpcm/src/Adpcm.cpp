#include <Kotonoha/Formats/Adpcm.hpp>

#include "../../internal/ByteReader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Kotonoha::Formats::Adpcm {
namespace {

using Kotonoha::Formats::readU16LE;
using Kotonoha::Formats::readU32LE;

// Standard 88-entry IMA-ADPCM step table (indices 0..87, matching MAIN.exe).
constexpr std::array<int32_t, 88> kImaStepTable = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794};

constexpr std::array<int32_t, 16> kImaIndexTable = {-1, -1, -1, -1, 2, 4, 6, 8,
                                                    -1, -1, -1, -1, 2, 4, 6, 8};
} // namespace

Adp4Decoded decodeAdp4(const uint8_t *data, size_t size) {
  Adp4Decoded out;
  if (!data || size < 4)
    return out;

  const uint32_t totalPairs = readU32LE(data);
  if (totalPairs == 0 || totalPairs > 48000000)
    return out;

  out.samples.reserve(static_cast<size_t>(totalPairs) * 2);

  int32_t predictor = 0;
  int32_t stepIndex = 0;
  uint32_t bitBuf = 0;
  uint8_t xorKey = 0;
  uint32_t pairsDone = 0;
  size_t pos = 4;

  auto decodeNibble = [&](uint8_t nib) {
    int32_t diff = ((2 * (nib & 7) + 1) * kImaStepTable[stepIndex]) >> 3;
    if (nib & 8)
      diff = -diff;
    predictor = std::clamp(predictor + diff, -32767, 32767);
    stepIndex = std::clamp(stepIndex + kImaIndexTable[nib], 0, 87);
    out.samples.push_back(static_cast<int16_t>(predictor));
  };

  while (pairsDone < totalPairs && pos < size) {
    if (bitBuf < 0x100) {
      bitBuf = static_cast<uint32_t>(data[pos++]) + 0x8000u;
    }
    const uint32_t flag = bitBuf & 1u;
    bitBuf >>= 1;

    if (pos >= size)
      break;
    const uint8_t b1 = data[pos++];
    uint32_t count = 0;
    if (b1 >= 0x80) {
      count = static_cast<uint32_t>(b1 & 0x7F) + 1;
    } else {
      if (pos >= size)
        break;
      const uint8_t b2 = data[pos++];
      count = ((static_cast<uint32_t>(b1) << 8) | b2) + 1;
    }

    const uint32_t remaining = totalPairs - pairsDone;
    const uint32_t actualCount = std::min(count, remaining);
    pairsDone += actualCount;

    if (flag == 1) {
      predictor = 0;
      stepIndex = 0;
      out.samples.insert(out.samples.end(),
                         static_cast<size_t>(actualCount) * 2, int16_t{0});
    } else {
      for (uint32_t i = 0; i < actualCount && pos < size; ++i) {
        const uint8_t byte = static_cast<uint8_t>(data[pos++] ^ xorKey++);
        decodeNibble(byte & 0x0F);
        decodeNibble((byte >> 4) & 0x0F);
      }
    }
  }

  // Compute lip-sync mouth frames every 2205 samples (0.1s @ 22050 Hz).
  constexpr size_t kLipStep = 2205;
  out.mouthFrames.reserve((out.samples.size() + kLipStep - 1) / kLipStep);
  for (size_t i = 0; i < out.samples.size(); i += kLipStep) {
    const int32_t s = out.samples[i];
    out.mouthFrames.push_back((s > 64 || s < -64) ? 0xFF : 0x00);
  }
  return out;
}
} // namespace Kotonoha::Formats::Adpcm
