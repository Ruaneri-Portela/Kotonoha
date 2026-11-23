// Overflow ADV DAT archive directory reader. Image/animation/audio codecs
// live in modules/formats and are re-exported by SnowRadish/Pack.hpp.
#include "SnowRadish/Pack.hpp"

#include "../../../formats/internal/ByteReader.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace SnowRadish {

size_t lzssDecode(const uint8_t *src, size_t srcSize, uint8_t *dst,
                  size_t dstSize) {
  std::array<uint8_t, 4096> window{};
  size_t wp = 0xFEE, in = 0, out = 0;
  while (out < dstSize && in < srcSize) {
    uint8_t flags = src[in++];
    for (int bit = 7; bit >= 0 && out < dstSize; --bit) {
      if (in >= srcSize)
        return in;
      if (flags & (1 << bit)) {
        if (in + 2 > srcSize)
          return in;
        unsigned d = src[in] | (src[in + 1] << 8);
        in += 2;
        unsigned len = (d & 0xF) + 3, pos = d >> 4;
        for (unsigned i = 0; i < len && out < dstSize; ++i) {
          uint8_t c = window[(pos + i) & 0xFFF];
          dst[out++] = c;
          window[wp++ & 0xFFF] = c;
        }
      } else {
        uint8_t c = src[in++];
        dst[out++] = c;
        window[wp++ & 0xFFF] = c;
      }
    }
  }
  return in;
}

std::vector<PackEntry> readDirectory(const std::vector<uint8_t> &pack) {
  std::vector<PackEntry> entries;
  if (pack.size() < 12)
    return entries;
  const size_t tail = pack.size() - 12;
  const uint32_t comp = rd32(&pack[tail]) ^ 0xF0F0F0F0u;
  const uint32_t raw = rd32(&pack[tail + 4]) ^ 0xF0F0F0F0u;
  if (comp == 0 || comp > tail || raw < 80 || raw > (64u << 20))
    return entries;
  const size_t dirStart = tail - comp;
  std::vector<uint8_t> dir(raw);
  lzssDecode(pack.data() + dirStart, comp, dir.data(), dir.size());

  const size_t count = raw / 80;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t *r = &dir[80 * i];
    PackEntry e;
    e.id = rd32(r);
    const char *n = reinterpret_cast<const char *>(r + 4);
    e.name.assign(n, strnlen(n, 64));
    e.offset = rd32(r + 68);
    e.compressedSize = rd32(r + 72);
    e.rawSize = rd32(r + 76);
    if (e.offset > dirStart || e.compressedSize > dirStart - e.offset)
      continue;
    entries.push_back(std::move(e));
  }
  return entries;
}

std::vector<uint8_t> extractEntry(const std::vector<uint8_t> &pack,
                                  const PackEntry &entry) {
  std::vector<uint8_t> out;
  if (entry.offset >= pack.size())
    return out;
  const size_t avail =
      std::min<size_t>(entry.compressedSize, pack.size() - entry.offset);
  const bool known = entry.rawSize != 0;
  if (known && entry.compressedSize == entry.rawSize) {
    // Stored without compression.
    out.assign(pack.begin() + entry.offset,
               pack.begin() + entry.offset + avail);
    return out;
  }
  // Worst case is 18 output bytes per 2 source bytes plus flags.
  out.resize(known ? entry.rawSize : avail * 10);
  const size_t produced =
      lzssDecode(pack.data() + entry.offset, avail, out.data(), out.size());
  (void)produced;
  if (!known) {
    while (!out.empty() && out.back() == 0)
      out.pop_back();
  }
  return out;
}

} // namespace SnowRadish
