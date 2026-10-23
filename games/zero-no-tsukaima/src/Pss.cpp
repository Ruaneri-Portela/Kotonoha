#include <ZeroNoTsukaima/Pss.hpp>

#include <algorithm>
#include <cstring>

namespace ZeroNoTsukaima {
namespace {

uint32_t Le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

int64_t Pts(const uint8_t *p) {
  return (int64_t(p[0] & 0x0e) << 29) | (int64_t(p[1]) << 22) | (int64_t(p[2] & 0xfe) << 14) | (int64_t(p[3]) << 7) |
         (p[4] >> 1);
}

bool DecodeAudio(const std::vector<uint8_t> &raw, PssMovie *out) {
  if (raw.size() < 8 || std::memcmp(raw.data(), "SShd", 4) != 0) return false;
  const uint32_t headerSize = Le32(raw.data() + 4);
  if (headerSize < 16 || 8 + size_t(headerSize) + 8 > raw.size()) return false;
  const uint8_t *h = raw.data() + 8;
  const uint32_t type = Le32(h), rate = Le32(h + 4), channels = Le32(h + 8), interleave = Le32(h + 12);
  const uint8_t *b = h + headerSize;
  if (type != 1 || channels < 1 || channels > 2 || interleave == 0 || interleave % 2 || std::memcmp(b, "SSbd", 4) != 0)
    return false;
  size_t body = Le32(b + 4);
  const uint8_t *src = b + 8;
  body = std::min(body, size_t(raw.data() + raw.size() - src));
  const size_t group = size_t(interleave) * channels;
  const size_t frames = body / group * (interleave / 2);
  out->pcm.assign(frames * channels, 0);
  const size_t per = interleave / 2;
  for (size_t g = 0; g < body / group; ++g)
    for (uint32_t c = 0; c < channels; ++c) {
      const uint8_t *blk = src + g * group + c * interleave;
      for (size_t i = 0; i < per; ++i)
        out->pcm[(g * per + i) * channels + c] = int16_t(uint16_t(blk[2 * i] | blk[2 * i + 1] << 8));
    }
  out->rate = int(rate);
  out->channels = int(channels);
  return true;
}

} // namespace

bool DemuxPss(const uint8_t *d, size_t size, PssMovie *out) {
  *out = PssMovie{};
  std::vector<uint8_t> audio;
  size_t p = 0;
  while (p + 6 <= size) {
    if (d[p] != 0 || d[p + 1] != 0 || d[p + 2] != 1) {
      ++p;
      continue;
    }
    const uint8_t id = d[p + 3];
    if (id == 0xBA) { // pack header (MPEG-2: 14 bytes + stuffing, MPEG-1: 12)
      p += (p + 13 < size && (d[p + 4] & 0xC0) == 0x40) ? 14 + (d[p + 13] & 7) : 12;
      continue;
    }
    if (id == 0xB9) break;
    if (id < 0xBB) {
      ++p;
      continue;
    }
    const size_t len = size_t(d[p + 4]) << 8 | d[p + 5];
    const uint8_t *body = d + p + 6;
    if (p + 6 + len > size) break;
    if ((id == 0xE0 || id == 0xBD) && len >= 3 && (body[0] & 0xC0) == 0x80) {
      const size_t hl = body[2];
      if (3 + hl <= len) {
        const uint8_t *pay = body + 3 + hl;
        const size_t n = len - 3 - hl;
        if (id == 0xE0) {
          PssMovie::Packet pkt;
          pkt.data.assign(pay, pay + n);
          if ((body[1] & 0x80) && hl >= 5) pkt.pts = Pts(body + 3);
          out->video.push_back(std::move(pkt));
        } else if (n > 4) {
          audio.insert(audio.end(), pay + 4, pay + n);
        }
      }
    }
    p += 6 + len;
  }
  if (!audio.empty()) DecodeAudio(audio, out);
  return !out->video.empty();
}

} // namespace ZeroNoTsukaima
