// Overflow ADV sprite/animation codecs (.ANM, .CHD, headerless .CHAR).
#include <SnowRadish/PackAnimation.hpp>

#include "../../../internal/ByteReader.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace SnowRadish {

bool decodeAnm(const std::vector<uint8_t> &d, Animation &out) {
  out = {};
  if (d.size() < 12)
    return false;
  const uint32_t count = rd32(d.data());
  if (!count)
    return false;
  auto fail = [&]() {
    out = {};
    return false;
  };
  // Cursor files use the general animation format, not the lip-sync header.
  if (rd32(&d[4]) >= 0x2084) {
    if (d.size() < 0x2084 || count > 1024)
      return false;
    out.cursor = true;
    for (uint32_t i = 0; i < count; ++i) {
      const size_t off = rd32(&d[4 + i * 8]), size = rd32(&d[8 + i * 8]);
      if (off < 0x2084 || off > d.size() || size > d.size() - off)
        return fail();
      Sprite frame;
      const std::vector<uint8_t> bytes(d.begin() + off, d.begin() + off + size);
      if (!decodeChd(bytes, frame) && !decodeChar(bytes, frame))
        return fail();
      out.frames.push_back(std::move(frame));
    }
    return true;
  }
  if (d.size() < 0x684 || count > 16)
    return false;
  // FUN_0040b16c: first word is the number of independently voiced regions.
  for (uint32_t region = 0; region < count; ++region) {
    const size_t h = size_t(region) * 96;
    const uint32_t x = rd32(&d[h + 76]), y = rd32(&d[h + 80]);
    const uint32_t width = rd32(&d[h + 92]), height = rd32(&d[h + 96]);
    if (!width || !height || width > 800 || height > 600 || x > 800 - width ||
        y > 600 - height)
      return fail();
    Animation::Region r{rd32(&d[h + 4]), out.frames.size(), 0};
    for (size_t i = 0; i < 3; ++i) {
      const size_t off = rd32(&d[h + 12 + i * 4]),
                   size = rd32(&d[h + 44 + i * 4]);
      if (!size || off < 0x684 || off > d.size() || size > d.size() - off)
        return fail();
      std::vector<uint8_t> bytes(d.begin() + off, d.begin() + off + size);
      Sprite frame;
      if (!(bytes.size() >= 24 && rd32(bytes.data()) == 1 &&
            decodeChd(bytes, frame)) &&
          !(bytes.size() != size_t(width) * height * 3 &&
            decodeChar(bytes, frame))) {
        if (bytes.size() != size_t(width) * height * 3)
          return fail();
        frame.width = width;
        frame.height = height;
        frame.x = int32_t(x);
        frame.y = int32_t(y);
        frame.rgba.resize(size_t(width) * height * 4);
        for (uint32_t row = 0; row < height; ++row) {
          const uint8_t *source =
              bytes.data() + size_t(height - 1 - row) * width * 3;
          uint8_t *target = frame.rgba.data() + size_t(row) * width * 4;
          for (uint32_t col = 0; col < width; ++col) {
            target[col * 4] = source[col * 3 + 2];
            target[col * 4 + 1] = source[col * 3 + 1];
            target[col * 4 + 2] = source[col * 3];
            target[col * 4 + 3] = 255;
          }
        }
      }
      out.frames.push_back(std::move(frame));
      ++r.count;
    }
    out.regions.push_back(r);
  }
  return true;
}

bool decodeChar(const std::vector<uint8_t> &d, Sprite &s) {
  if (d.empty())
    return false;
  std::vector<uint8_t> headed(20, 0);
  headed[0] = 1;
  headed.insert(headed.end(), d.begin(), d.end());
  return decodeChd(headed, s);
}

bool decodeChd(const std::vector<uint8_t> &d, Sprite &s) {
  if (d.size() < 24)
    return false;
  auto u32 = [&](size_t o) {
    return uint32_t(d[o]) | d[o + 1] << 8 | d[o + 2] << 16 |
           uint32_t(d[o + 3]) << 24;
  };
  if (u32(0) != 1)
    return false;
  constexpr uint32_t W = 800, H = 600;
  std::vector<uint8_t> canvas(size_t(W) * H * 4, 0);
  uint32_t minX = W, minY = H, maxX = 0, maxY = 0;
  auto put = [&](uint32_t x, uint32_t y, size_t p, uint8_t a) {
    if (x >= W || a == 0)
      return;
    uint8_t *px = &canvas[(size_t(y) * W + x) * 4];
    px[0] = d[p + 2];
    px[1] = d[p + 1];
    px[2] = d[p];
    px[3] = a;
    minX = std::min(minX, x);
    maxX = std::max(maxX, x);
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
  };
  size_t p = 20;
  for (uint32_t row = 0; row < H && p < d.size(); ++row) {
    const uint32_t y = H - 1 - row;
    uint32_t x = 0;
    while (p < d.size()) {
      const uint8_t c = d[p++];
      if (c == 0xFF)
        break;
      if (c >= 0x9F) {
        x += c - 0x9E;
        continue;
      }
      if (p + 3 > d.size())
        return false;
      if (c >= 0x7F) {
        for (uint32_t n = c - 0x7E; n > 0; --n, ++x)
          put(x, y, p, 255);
      } else {
        put(x, y, p, uint8_t(std::min(255, c * 255 / 0x67)));
        ++x;
      }
      p += 3;
    }
  }
  if (minX > maxX || minY > maxY) {
    s.width = s.height = 1;
    s.x = s.y = 0;
    s.rgba.assign(4, 0);
    return true;
  }
  s.x = int32_t(minX);
  s.y = int32_t(minY);
  s.width = maxX - minX + 1;
  s.height = maxY - minY + 1;
  s.rgba.resize(size_t(s.width) * s.height * 4);
  for (uint32_t y = 0; y < s.height; ++y)
    std::copy_n(&canvas[(size_t(minY + y) * W + minX) * 4], s.width * 4,
                &s.rgba[size_t(y) * s.width * 4]);
  return true;
}

} // namespace SnowRadish
