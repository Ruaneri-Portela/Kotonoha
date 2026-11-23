// Overflow ADV image codecs (.EVD pictures and alpha masks).
#include <SnowRadish/PackImage.hpp>

#include "../../../internal/ByteReader.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace SnowRadish {

bool decodeEvd(const std::vector<uint8_t> &d, Image &img, bool hasHeader) {
  uint32_t w = 0, h = 0;
  size_t p = 0;
  if (hasHeader) {
    if (d.size() < 12 || (rd32(&d[0]) != 3 && rd32(&d[0]) != 0))
      return false;
    w = rd32(&d[4]);
    h = rd32(&d[8]);
    p = 12;
    if (w == 0 || h == 0 || w > 8192 || h > 8192)
      return false;
  } else {
    // Pure Mail CRGB files are EVD type-3 delta data without the 12-byte
    // header. Assume 800x600, the standard Snow Radish resolution.
    w = 800;
    h = 600;
    p = 0;
  }

  static const std::vector<uint32_t> table = [] {
    std::vector<uint32_t> t(0x8000);
    for (int i = 0; i < 0x8000; ++i) {
      int r = (i >> 10) & 31, g = (i >> 5) & 31, b = i & 31;
      if (r > 15)
        r -= 32;
      if (g > 15)
        g -= 32;
      if (b > 15)
        b -= 32;
      t[i] = static_cast<uint32_t>((r << 16) + (g << 8) + b);
    }
    return t;
  }();

  const size_t total = static_cast<size_t>(w) * h;
  std::vector<uint8_t> bgr(total * 3);
  size_t px = 0;
  uint32_t cur = 0;
  auto put = [&](uint32_t c) {
    bgr[px * 3] = c & 255;
    bgr[px * 3 + 1] = (c >> 8) & 255;
    bgr[px * 3 + 2] = (c >> 16) & 255;
    ++px;
  };
  const bool raw = hasHeader && rd32(&d[0]) == 0; // type 0: uncompressed BGR
  if (raw) {
    if (d.size() < 12 + total * 3)
      return false;
    std::copy(d.begin() + 12, d.begin() + 12 + long(total * 3), bgr.begin());
    px = total;
  }
  while (px < total && p < d.size()) {
    const uint8_t a = d[p];
    if (a < 0x80) {
      if (p + 2 > d.size())
        return false;
      cur += table[(a << 8) | d[p + 1]];
      p += 2;
      put(cur);
    } else if (a < 0xC0) {
      ++p;
      for (int n = a - 0x7F; n > 0 && px < total; --n)
        put(cur);
    } else if (a == 0xFF) {
      ++p; // end-of-line marker, not used in full-frame streams
    } else {
      ++p;
      for (int n = a - 0xBF; n > 0 && px < total; --n) {
        if (p + 3 > d.size())
          return false;
        cur = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16);
        p += 3;
        put(cur);
      }
    }
  }
  if (px != total)
    return false;

  img.width = w;
  img.height = h;
  img.rgb.resize(total * 3);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t *s = &bgr[static_cast<size_t>(h - 1 - y) * w * 3];
    uint8_t *o = &img.rgb[static_cast<size_t>(y) * w * 3];
    for (uint32_t x = 0; x < w; ++x) {
      o[x * 3] = s[x * 3 + 2];
      o[x * 3 + 1] = s[x * 3 + 1];
      o[x * 3 + 2] = s[x * 3];
    }
  }
  return true;
}

bool decodeAlpha(const std::vector<uint8_t> &d, bool alternate,
                 AlphaMask &out) {
  const uint32_t width = alternate ? 250 : 200;
  const uint32_t height = alternate ? 200 : 150;
  if (d.size() != size_t(width) * height)
    return false;
  out.width = width;
  out.height = height;
  out.alpha = d;
  return true;
}

} // namespace SnowRadish
