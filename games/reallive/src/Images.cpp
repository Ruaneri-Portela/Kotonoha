#include "RealLive/Images.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace RealLive {
namespace {
using Bytes = std::vector<std::uint8_t>;
[[noreturn]] void bad(const char *why) { throw std::runtime_error(why); }
void range(const Bytes &b, std::size_t p, std::size_t n) {
  if (p > b.size() || n > b.size() - p)
    bad("G00: truncated data");
}
std::uint16_t u16(const Bytes &b, std::size_t p) {
  range(b, p, 2);
  return static_cast<std::uint16_t>(std::uint16_t(b[p]) |
                                    (std::uint16_t(b[p + 1]) << 8));
}
std::uint32_t u32(const Bytes &b, std::size_t p) {
  range(b, p, 4);
  return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) |
         (std::uint32_t(b[p + 2]) << 16) | (std::uint32_t(b[p + 3]) << 24);
}
std::int32_t s32(const Bytes &b, std::size_t p) {
  return static_cast<std::int32_t>(u32(b, p));
}
constexpr std::size_t limit = 512 * 1024 * 1024;
Bytes unpack(const Bytes &b, std::size_t header, bool rgb) {
  const auto packed = u32(b, header);
  const auto declared = u32(b, header + 4);
  const auto size = rgb ? std::uint32_t(u16(b, 1)) * u16(b, 3) * 3 : declared;
  if (rgb && declared != size &&
      declared != std::uint64_t(u16(b, 1)) * u16(b, 3) * 4)
    bad("G00: invalid RGB output size");
  if (packed < 8 || packed != b.size() - header || size > limit)
    bad("G00: invalid compressed size");
  Bytes out;
  out.reserve(size);
  std::size_t p = header + 8;
  while (out.size() < size) {
    range(b, p, 1);
    const auto flags = b[p++];
    for (unsigned bit = 0; bit < 8 && out.size() < size; ++bit) {
      if (flags & (1u << bit)) {
        const std::size_t n = rgb ? 3 : 1;
        range(b, p, n);
        if (n > size - out.size())
          bad("G00: literal exceeds output");
        for (std::size_t i = 0; i < n; ++i)
          out.push_back(b[p++]);
      } else {
        const auto token = u16(b, p);
        p += 2;
        const std::size_t unit = rgb ? 3 : 1;
        const std::size_t distance = (token >> 4) * unit;
        const std::size_t count = ((token & 15) + (rgb ? 1 : 2)) * unit;
        if (!distance || distance > out.size() || count > size - out.size())
          bad("G00: invalid backreference");
        for (std::size_t i = 0; i < count; ++i)
          out.push_back(out[out.size() - distance]);
      }
    }
  }
  // Encoders may leave unused final flag tokens or zero padding.
  // The declared packed size still bounds every read.
  return out;
}
void pixel(Image &im, std::size_t dst, const Bytes &b, std::size_t src,
           bool alpha) {
  im.rgba[dst] = b[src + 2];
  im.rgba[dst + 1] = b[src + 1];
  im.rgba[dst + 2] = b[src];
  im.rgba[dst + 3] = alpha ? b[src + 3] : 255;
}
void allocate(Image &im) {
  const std::uint64_t n = std::uint64_t(im.width) * im.height * 4;
  if (!im.width || !im.height || n > limit)
    bad("G00: invalid canvas size");
  im.rgba.resize(static_cast<std::size_t>(n));
}
} // namespace

Image decodeG00(const Bytes &bytes) {
  range(bytes, 0, 5);
  Image im;
  im.width = im.sourceWidth = u16(bytes, 1);
  im.height = im.sourceHeight = u16(bytes, 3);
  const auto type = bytes[0];
  if (type > 2)
    bad("G00: unsupported type");
  if (type < 2) {
    allocate(im);
    const auto raw = unpack(bytes, 5, type == 0);
    const std::size_t pixels = std::size_t(im.width) * im.height;
    if (type == 0) {
      if (raw.size() != pixels * 3)
        bad("G00: RGB size mismatch");
      for (std::size_t i = 0; i < pixels; ++i)
        pixel(im, i * 4, raw, i * 3, false);
    } else {
      const auto colors = u16(raw, 0);
      if (!colors || colors > 256)
        bad("G00: invalid palette");
      const std::size_t start = 2 + colors * 4;
      range(raw, start, pixels);
      if (raw.size() != start + pixels)
        bad("G00: indexed size mismatch");
      for (std::size_t i = 0; i < pixels; ++i) {
        if (raw[start + i] >= colors)
          bad("G00: palette index out of range");
        pixel(im, i * 4, raw, 2 + raw[start + i] * 4, false);
      }
    }
    im.regions.push_back({0, 0, static_cast<std::int32_t>(im.width) - 1,
                          static_cast<std::int32_t>(im.height) - 1, 0, 0, 0,
                          0});
    return im;
  }
  const auto count = u32(bytes, 5);
  if (count > 65535)
    bad("G00: too many regions");
  range(bytes, 9, std::size_t(count) * 24);
  for (std::size_t i = 0; i < count; ++i) {
    const auto p = 9 + i * 24;
    ImageRegion r{s32(bytes, p),
                  s32(bytes, p + 4),
                  s32(bytes, p + 8),
                  s32(bytes, p + 12),
                  s32(bytes, p + 16),
                  s32(bytes, p + 20),
                  0,
                  0};
    // Retail files may use out-of-canvas bounds for an empty pattern.
    r.atlasX = r.x1;
    r.atlasY = r.y1;
    im.regions.push_back(r);
  }
  bool stacked = count > 1;
  for (const auto &r : im.regions) {
    const auto &first = im.regions.front();
    if (r.x1 != first.x1 || r.y1 != first.y1 || r.x2 != first.x2 ||
        r.y2 != first.y2)
      stacked = false;
  }
  if (stacked) {
    if (std::uint64_t(im.height) * count >
        std::numeric_limits<std::uint32_t>::max())
      bad("G00: atlas size overflow");
    im.height *= count;
    for (std::size_t i = 0; i < count; ++i) {
      const auto y = std::int64_t(im.regions[i].atlasY) +
                     std::int64_t(i) * im.sourceHeight;
      if (y > std::numeric_limits<std::int32_t>::max())
        bad("G00: atlas origin overflow");
      im.regions[i].atlasY = static_cast<std::int32_t>(y);
    }
  }
  allocate(im);
  const auto raw = unpack(bytes, 9 + std::size_t(count) * 24, false);
  if (u32(raw, 0) != count)
    bad("G00: region table count mismatch");
  range(raw, 4, std::size_t(count) * 8);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t offset = u32(raw, 4 + i * 8);
    const auto signedLength = s32(raw, 8 + i * 8);
    if (signedLength <= 0)
      continue; // Retail REALLIVE uses negative lengths for empty patterns.
    const std::size_t length = static_cast<std::size_t>(signedLength);
    range(raw, offset, length);
    if (length < 0x74)
      bad("G00: invalid region block");
    const auto end = offset + length;
    for (std::size_t p = offset + 0x74; p < end;) {
      if (end - p < 0x5c)
        bad("G00: truncated tile header");
      const std::int64_t x =
          std::int64_t(std::int16_t(u16(raw, p))) + im.regions[i].atlasX;
      const std::int64_t y =
          std::int64_t(std::int16_t(u16(raw, p + 2))) + im.regions[i].atlasY;
      const std::size_t w = u16(raw, p + 6), h = u16(raw, p + 8);
      p += 0x5c;
      const auto n = w * h * 4;
      if (n > end - p)
        bad("G00: truncated tile pixels");
      if (x < 0 || y < 0 || x + static_cast<std::int64_t>(w) > im.width ||
          y + static_cast<std::int64_t>(h) > im.height)
        bad("G00: tile outside canvas");
      for (std::size_t row = 0; row < h; ++row)
        for (std::size_t col = 0; col < w; ++col)
          pixel(im,
                ((static_cast<std::size_t>(y) + row) * im.width +
                 static_cast<std::size_t>(x) + col) *
                    4,
                raw, p + (row * w + col) * 4, true);
      p += n;
    }
  }
  return im;
}
Image decodePDT(const Bytes &bytes) {
  range(bytes, 0, 32);
  if (bytes[0] != 'P' || bytes[1] != 'D' || bytes[2] != 'T' ||
      bytes[3] != '1' || (bytes[4] != '0' && bytes[4] != '1'))
    bad("PDT: unsupported signature");
  if (u32(bytes, 8) != bytes.size())
    bad("PDT: file size mismatch");
  Image im;
  im.width = im.sourceWidth = u32(bytes, 12);
  im.height = im.sourceHeight = u32(bytes, 16);
  allocate(im);
  const std::size_t count = std::size_t(im.width) * im.height;
  const std::size_t mask = u32(bytes, 28), begin = bytes[4] == '0' ? 32 : 0x460;
  if (mask && (mask < begin || mask > bytes.size()))
    bad("PDT: invalid mask offset");
  const auto imageEnd = mask ? mask : bytes.size();
  if (begin > imageEnd)
    bad("PDT: truncated image header");
  // PDT flags run from most significant to least significant bit. All
  // distances below are measured in pixels (indices for PDT11).
  auto expand = [&](std::size_t start, std::size_t end, int mode) {
    Bytes out;
    const std::size_t unit = mode == 0 ? 3 : 1;
    out.reserve(count * unit);
    std::size_t p = start;
    auto require = [&](std::size_t n) {
      if (p > end || n > end - p)
        bad("PDT: truncated compressed stream");
    };
    while (out.size() < count * unit) {
      require(1);
      const auto flags = bytes[p++];
      for (unsigned bit = 0; bit < 8 && out.size() < count * unit; ++bit) {
        if (flags & (0x80u >> bit)) {
          require(unit);
          for (std::size_t k = 0; k < unit; ++k)
            out.push_back(bytes[p++]);
        } else {
          std::size_t distance, length;
          if (mode == 1) {
            require(1);
            const auto t = bytes[p++];
            distance = u32(bytes, 0x420 + (t & 15) * 4);
            length = (t >> 4) + 2;
          } else {
            require(2);
            const auto t = u16(bytes, p);
            p += 2;
            if (mode == 0) {
              distance = (t >> 4) + 1;
              length = (t & 15) + 1;
            } else {
              distance = (t >> 8) + 1;
              length = (t & 255) + 2;
            }
          }
          distance *= unit;
          length *= unit;
          if (!distance || distance > out.size() ||
              length > count * unit - out.size())
            bad("PDT: invalid backreference");
          for (std::size_t k = 0; k < length; ++k)
            out.push_back(out[out.size() - distance]);
        }
      }
    }
    return out;
  };
  const bool indexed = bytes[4] == '1';
  auto raw = expand(begin, imageEnd, indexed ? 1 : 0);
  for (std::size_t i = 0; i < count; ++i) {
    if (indexed)
      pixel(im, i * 4, bytes, 32 + std::size_t(raw[i]) * 4, false);
    else
      pixel(im, i * 4, raw, i * 3, false);
  }
  if (mask) {
    auto alpha = expand(mask, bytes.size(), 2);
    for (std::size_t i = 0; i < count; ++i)
      im.rgba[i * 4 + 3] = alpha[i];
  }
  im.regions.push_back({0, 0, static_cast<std::int32_t>(im.width) - 1,
                        static_cast<std::int32_t>(im.height) - 1, 0, 0, 0, 0});
  return im;
}
Anm decodeANM(const Bytes &bytes) {
  static constexpr std::uint8_t magic[12] = {'A', 'N', 'M', '3', '2', 0,
                                             0,   0,   0,   1,   0,   0};
  if (bytes.size() < 0xb8)
    bad("ANM: truncated header");
  if (std::memcmp(bytes.data(), magic, sizeof(magic)) != 0)
    bad("ANM: invalid signature");

  Anm anm;
  const char *name = reinterpret_cast<const char *>(bytes.data() + 0x1c);
  const std::size_t nameLimit =
      std::min<std::size_t>(0x8c - 0x1c, bytes.size() - 0x1c);
  std::size_t nameLength = 0;
  while (nameLength < nameLimit && name[nameLength])
    ++nameLength;
  if (!nameLength)
    bad("ANM: missing image name");
  anm.image.assign(name, nameLength);

  const auto framesLen = s32(bytes, 0x8c);
  const auto listLen = s32(bytes, 0x90);
  const auto setLen = s32(bytes, 0x94);
  if (framesLen < 0 || framesLen > 4096 || listLen < 0 || listLen > 4096 ||
      setLen < 0 || setLen > 4096)
    bad("ANM: invalid table size");
  const std::size_t frameBase = 0xb8;
  const std::size_t listBase = frameBase + std::size_t(framesLen) * 0x60;
  const std::size_t setBase = listBase + std::size_t(listLen) * 0x68;
  const std::size_t end = setBase + std::size_t(setLen) * 0x78;
  if (end > bytes.size())
    bad("ANM: truncated tables");

  for (std::int32_t i = 0; i < framesLen; ++i) {
    const std::size_t p = frameBase + std::size_t(i) * 0x60;
    anm.frames.push_back({s32(bytes, p), s32(bytes, p + 4), s32(bytes, p + 8),
                          s32(bytes, p + 12), s32(bytes, p + 16),
                          s32(bytes, p + 20), s32(bytes, p + 0x38)});
  }
  auto readList = [&](std::size_t base, std::size_t stride,
                      std::vector<std::vector<int>> &out) {
    const auto length = s32(bytes, base + 4);
    if (length < 0 || std::size_t(length) * 4 > stride - 8)
      bad("ANM: invalid list length");
    std::vector<int> values;
    values.reserve(std::size_t(length));
    for (std::int32_t j = 0; j < length; ++j)
      values.push_back(s32(bytes, base + 8 + std::size_t(j) * 4));
    out.push_back(std::move(values));
  };
  for (std::int32_t i = 0; i < listLen; ++i)
    readList(listBase + std::size_t(i) * 0x68, 0x68, anm.frameLists);
  for (std::int32_t i = 0; i < setLen; ++i)
    readList(setBase + std::size_t(i) * 0x78, 0x78, anm.sets);
  return anm;
}
} // namespace RealLive
