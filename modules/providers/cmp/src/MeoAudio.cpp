#include <SummerRadish/MeoAudio.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Kotonoha::Providers::MeoAudio {
namespace {
constexpr int steps[] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int indexChange[] = {-1, -1, -1, -1, 2, 4, 6, 8};

std::uint32_t word(const std::vector<Uint8> &bytes, std::size_t offset) {
  return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset + 1]) << 8 |
         std::uint32_t(bytes[offset + 2]) << 16 |
         std::uint32_t(bytes[offset + 3]) << 24;
}

int16_t sample(int nibble, int &index, int &predictor) {
  // The original MEO module uses a precomputed IMA table. Its positive
  // entries are floor(step * (2*n + 1) / 8); negative entries are -value - 1.
  const int magnitude = steps[index] * (2 * (nibble & 7) + 1) / 8;
  predictor += nibble & 8 ? -magnitude - 1 : magnitude;
  predictor = std::clamp(predictor, -32767, 32767);
  index = std::clamp(index + indexChange[nibble & 7], 0, 88);
  return static_cast<int16_t>(predictor);
}
} // namespace

Bgm decodeBgm(const std::vector<Uint8> &bytes) {
  if (bytes.size() < 17)
    throw std::runtime_error("MEO BGM header is truncated");
  const std::size_t intro = word(bytes, 0);
  const std::size_t loop = word(bytes, 4);
  if (!intro || intro + loop != bytes.size() - 16)
    throw std::runtime_error("Invalid MEO BGM segment lengths");
  if (bytes.size() - 16 > std::numeric_limits<int>::max() / 4)
    throw std::runtime_error("MEO BGM is too large");

  Bgm result;
  result.loopStart = intro;
  result.clip.spec = {SDL_AUDIO_S16, 2, 44100};
  result.clip.data.resize((bytes.size() - 16) * 4);
  int index[2] = {0, 0};
  int predictor[2] = {0, 0};
  for (std::size_t i = 16; i < bytes.size(); ++i) {
    // The original player resets both channel states before the loop segment.
    if (i - 16 == intro) {
      index[0] = index[1] = 0;
      predictor[0] = predictor[1] = 0;
    }
    const int16_t left = sample(bytes[i] & 15, index[0], predictor[0]);
    const int16_t right = sample(bytes[i] >> 4, index[1], predictor[1]);
    const std::size_t offset = (i - 16) * 4;
    std::memcpy(result.clip.data.data() + offset, &left, sizeof(left));
    std::memcpy(result.clip.data.data() + offset + 2, &right, sizeof(right));
  }
  return result;
}
} // namespace Kotonoha::Providers::MeoAudio
