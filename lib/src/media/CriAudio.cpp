#include <Kotonoha/media/CriAudio.hpp>
#include <Kotonoha/media/MediaDecoder.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
namespace Kotonoha {
namespace {
uint16_t be16(const uint8_t *p) { return uint16_t(p[0]) << 8 | p[1]; }
uint32_t be32(const uint8_t *p) {
  return uint32_t(be16(p)) << 16 | be16(p + 2);
}
class Bits {
  std::vector<uint8_t> &data;
  size_t end;

public:
  size_t pos;
  Bits(std::vector<uint8_t> &b, size_t start, size_t stop)
      : data(b), end(stop * 8), pos(start * 8) {}
  unsigned get(unsigned n) {
    if (pos > end || n > end - pos)
      throw std::runtime_error("Truncated AHX scale factors");
    unsigned v = 0;
    while (n--) {
      v = v * 2 + ((data[pos / 8] >> (7 - pos % 8)) & 1);
      ++pos;
    }
    return v;
  }
  void put2(size_t p, unsigned v) {
    for (unsigned j = 0; j < 2; ++j) {
      auto mask = uint8_t(1u << (7 - (p + j) % 8));
      auto &b = data[(p + j) / 8];
      b = uint8_t((b & ~mask) | (((v >> (1 - j)) & 1) << (7 - (p + j) % 8)));
    }
  }
};
} // namespace
std::vector<Uint8> decryptCriAudio(const std::vector<Uint8> &bytes,
                                   CriKey8 key) {
  if (bytes.size() < 24 || be16(bytes.data()) != 0x8000)
    throw std::runtime_error("Invalid CRI audio header");
  auto out = bytes;
  size_t start = size_t(be16(out.data() + 2)) + 4;
  if (start < 24 || start > out.size() ||
      std::memcmp(out.data() + start - 6, "(c)CRI", 6))
    throw std::runtime_error("Invalid CRI audio offset");
  if (!out[0x13])
    return out;
  if (out[0x13] != 8)
    throw std::runtime_error("Unsupported CRI audio cipher");
  if (out[4] == 0x10 || out[4] == 0x11) {
    const std::array<uint16_t, 3> keys{key.seed, key.multiplier, key.increment};
    for (size_t p = start; p + 16 < out.size();) {
      if (p + 4 > out.size() || out[p] != 0xff)
        throw std::runtime_error("Invalid AHX frame sync");
      // AHX strips MPEG padding. The next identical frame sync bounds the
      // bitstream; its SCFSI low bits are enciphered independently per frame.
      size_t next = p + 4;
      while (next + 16 < out.size() && next - p <= 0x414 &&
             std::memcmp(out.data() + p, out.data() + next, 4))
        ++next;
      if (next - p > 0x414)
        throw std::runtime_error("AHX frame exceeds maximum size");
      Bits bits(out, p + 4, next);
      std::array<unsigned, 30> allocation{};
      for (size_t i = 0; i < 30; ++i)
        allocation[i] = bits.get(i < 4 ? 4 : i < 11 ? 3 : 2);
      unsigned first = allocation[0] ? bits.get(2) : 0;
      uint16_t cipher = first ? keys[first - 1] : 0;
      for (size_t i = 1; i < 30; ++i) {
        if (allocation[i]) {
          size_t at = bits.pos;
          unsigned value = bits.get(2) ^ (cipher & 3);
          bits.put2(at, value);
        }
        cipher >>= 2;
      }
      p = next;
    }
  } else if (out[4] == 3) {
    unsigned frameSize = out[5], channels = out[7];
    if (frameSize < 3 || channels == 0 || channels > 8)
      throw std::runtime_error("Invalid ADX frame layout");
    const uint64_t samples = be32(out.data() + 12),
                   frameSamples = (frameSize - 2) * 2;
    const uint64_t frames =
        ((samples + frameSamples - 1) / frameSamples) * channels;
    if (frames > (out.size() - start) / frameSize)
      throw std::runtime_error("Truncated ADX samples");
    uint16_t cipher = key.seed;
    for (uint64_t i = 0; i < frames; ++i) {
      auto *p = out.data() + start + i * frameSize;
      uint16_t scale = (be16(p) ^ cipher) & 0x1fff;
      p[0] = uint8_t(scale >> 8);
      p[1] = uint8_t(scale);
      cipher = uint16_t((uint32_t(cipher) * key.multiplier + key.increment) &
                        0x7fff);
    }
  } else
    throw std::runtime_error("Unsupported encrypted CRI encoding");
  out[0x13] = 0;
  return out;
}
AudioClip decodeCriAudio(const std::vector<Uint8> &bytes, CriKey8 key) {
  auto clear = decryptCriAudio(bytes, key);
  auto clip = MediaDecoder::decodeAudio(clear);
  const uint32_t frames = be32(clear.data() + 12),
                 rate = be32(clear.data() + 8);
  if (clip.spec.format != SDL_AUDIO_F32 || clip.spec.channels != clear[7] ||
      uint32_t(clip.spec.freq) != rate)
    throw std::runtime_error("CRI decoded layout differs from container");
  size_t expected = size_t(frames) * clip.spec.channels * sizeof(float);
  if (clip.data.size() < expected)
    throw std::runtime_error("CRI decoder returned too few samples");
  clip.data.resize(expected); // exclude MPEG padding and ADX terminal blocks
  return clip;
}
} // namespace Kotonoha
