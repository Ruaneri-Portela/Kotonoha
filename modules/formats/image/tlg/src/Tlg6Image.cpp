// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Bounded portable TLG6 Golomb/MED/AVG image decoder. No game files required.
#include "../../internal/include/Kotonoha/Formats/DecodedData.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kotonoha::Formats {
namespace {
constexpr std::uint8_t kMagic[11] = {'T', 'L', 'G', '6', '.', '0',
                                     0,   'r', 'a', 'w', 0x1a};
constexpr int kWidthBlock = 8, kHeightBlock = 8;
constexpr std::size_t kMaxPixels = 32u * 1024u * 1024u;

struct Cursor {
  const std::uint8_t *data;
  std::size_t length;
  std::size_t offset = 0;

  const std::uint8_t *take(std::size_t count) {
    if (offset > length || count > length - offset)
      throw std::runtime_error("KiriKiri: truncated TLG6 image");
    const auto *at = data + offset;
    offset += count;
    return at;
  }
  std::uint32_t u32() {
    const auto *p = take(4);
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
  }
};

struct Bits {
  const std::uint8_t *data;
  std::size_t bitLength;
  std::size_t bitPosition = 0;

  unsigned bit() {
    if (bitPosition >= bitLength)
      throw std::runtime_error("KiriKiri: truncated TLG6 Golomb bitstream");
    const auto value = (data[bitPosition / 8] >> (bitPosition % 8)) & 1u;
    ++bitPosition;
    return value;
  }
  std::size_t gamma() {
    // Elias gamma, with an LSB-first suffix as used by TLG6.
    std::size_t zeros = 0;
    while (bit() == 0) {
      if (++zeros > 20)
        throw std::runtime_error("KiriKiri: excessive TLG6 run length");
    }
    std::size_t count = std::size_t(1) << zeros;
    for (std::size_t i = 0; i < zeros; ++i)
      count |= std::size_t(bit()) << i;
    return count;
  }
  unsigned rice(unsigned k) {
    unsigned quotient = 0;
    while (bit() == 0) {
      if (++quotient > 65535)
        throw std::runtime_error("KiriKiri: invalid TLG6 Rice quotient");
    }
    unsigned remainder = 0;
    for (unsigned i = 0; i < k; ++i)
      remainder |= bit() << i;
    return (quotient << k) | remainder;
  }
};

// Index [remaining nonzero values in adaptation period][running magnitude].
// The compressed thresholds originate from the TLG6 file-format algorithm.
unsigned golombK(unsigned remaining, unsigned magnitude) {
  static constexpr int counts[4][9] = {{3, 7, 15, 27, 63, 108, 223, 448, 130},
                                       {3, 5, 13, 24, 51, 95, 192, 384, 257},
                                       {2, 5, 12, 21, 39, 86, 155, 320, 384},
                                       {2, 3, 9, 18, 33, 61, 129, 258, 511}};
  const auto row = std::min(remaining, 3u);
  int accumulated = 0;
  for (unsigned k = 0; k < 9; ++k) {
    accumulated += counts[row][k];
    if (magnitude < static_cast<unsigned>(accumulated))
      return k;
  }
  return 8;
}

void decodeGolomb(const std::uint8_t *bytes, std::size_t bitLength,
                  std::size_t pixelCount, unsigned channel,
                  std::vector<std::array<std::uint8_t, 4>> &pixels) {
  Bits bits{bytes, bitLength};
  const bool firstRunIsZero = bits.bit() == 0;
  bool zeroRun = firstRunIsZero;
  std::size_t output = 0;
  unsigned period = 3, magnitude = 0;
  while (output < pixelCount) {
    const auto run = bits.gamma();
    if (run > pixelCount - output)
      throw std::runtime_error("KiriKiri: TLG6 run exceeds image block");
    if (zeroRun) {
      for (std::size_t i = 0; i < run; ++i)
        pixels[output++][channel] = 0;
    } else {
      for (std::size_t i = 0; i < run; ++i) {
        const auto code = bits.rice(golombK(period, magnitude));
        const auto abs = code >> 1;
        magnitude += abs;
        // Encoded integers are -1, +1, -2, +2, ... in two's complement.
        const int residual = (code & 1u) ? int(abs) + 1 : -int(abs) - 1;
        pixels[output++][channel] = static_cast<std::uint8_t>(residual);
        if (period-- == 0) {
          magnitude >>= 1;
          period = 3;
        }
      }
    }
    zeroRun = !zeroRun;
  }
}

// TLG6's filter type metadata uses the same 4096-byte slide compression as
// TLG5, but its initial dictionary contains 32 x 16 pairs of repeated bytes.
std::vector<std::uint8_t> decodeFilterTypes(const std::uint8_t *data,
                                            std::size_t length,
                                            std::size_t expected) {
  std::array<std::uint8_t, 4096> dict{};
  std::size_t index = 0;
  for (unsigned i = 0; i < 32; ++i)
    for (unsigned j = 0; j < 16; ++j) {
      for (unsigned k = 0; k < 4; ++k)
        dict[index++] = i;
      for (unsigned k = 0; k < 4; ++k)
        dict[index++] = j;
    }
  std::vector<std::uint8_t> output;
  output.reserve(expected);
  std::size_t pos = 0, ring = 0;
  unsigned flags = 0, remaining = 0;
  const auto add = [&](std::uint8_t value) {
    if (output.size() >= expected)
      throw std::runtime_error("KiriKiri: excessive TLG6 filter output");
    output.push_back(value);
    dict[ring] = value;
    ring = (ring + 1) & 4095u;
  };
  while (pos < length) {
    if (!remaining) {
      flags = data[pos++];
      remaining = 8;
    }
    if (flags & 1u) {
      if (length - pos < 2)
        throw std::runtime_error("KiriKiri: incomplete TLG6 filter reference");
      const unsigned a = data[pos++], b = data[pos++];
      std::size_t copy = a | ((b & 15u) << 8);
      std::size_t count = 3 + (b >> 4);
      if (count == 18) {
        if (pos == length)
          throw std::runtime_error("KiriKiri: incomplete TLG6 filter length");
        count += data[pos++];
      }
      if (count > expected - output.size())
        throw std::runtime_error(
            "KiriKiri: TLG6 filter reference overruns data");
      while (count--) {
        const auto value = dict[copy];
        copy = (copy + 1) & 4095u;
        add(value);
      }
    } else {
      if (pos == length)
        throw std::runtime_error("KiriKiri: incomplete TLG6 filter literal");
      add(data[pos++]);
    }
    flags >>= 1;
    --remaining;
  }
  if (output.size() != expected)
    throw std::runtime_error("KiriKiri: truncated TLG6 filter list");
  for (const auto code : output)
    if (code >= 32)
      throw std::runtime_error("KiriKiri: invalid TLG6 color filter");
  return output;
}

std::array<int, 3> unfilter(std::uint8_t filter,
                            const std::array<std::uint8_t, 4> &residual) {
  const int b = static_cast<std::int8_t>(residual[0]);
  const int g = static_cast<std::int8_t>(residual[1]);
  const int r = static_cast<std::int8_t>(residual[2]);
  switch (filter >> 1) {
  case 0:
    return {b, g, r};
  case 1:
    return {b + g, g, r + g};
  case 2:
    return {b, g + b, r + b + g};
  case 3:
    return {b + r + g, g + r, r};
  case 4:
    return {b + r, g + b + r, r + b + r + g};
  case 5:
    return {b + r, g + b + r, r};
  case 6:
    return {b + g, g, r};
  case 7:
    return {b, g + b, r};
  case 8:
    return {b, g, r + g};
  case 9:
    return {b + g + r + b, g + r + b, r + b};
  case 10:
    return {b + r, g + r, r};
  case 11:
    return {b, g + b, r + b};
  case 12:
    return {b, g + r + b, r + b};
  case 13:
    return {b + g, g + r + b + g, r + b + g};
  case 14:
    return {b + g + r, g + r, r + b + g + r};
  case 15:
    return {b, g + 2 * b, r + 2 * b};
  default:
    throw std::runtime_error("KiriKiri: unknown TLG6 color filter");
  }
}

std::uint8_t predict(unsigned left, unsigned above, unsigned upperLeft,
                     bool average) {
  if (average)
    return static_cast<std::uint8_t>((left + above + 1) / 2);
  if (upperLeft >= std::max(left, above))
    return static_cast<std::uint8_t>(std::min(left, above));
  if (upperLeft <= std::min(left, above))
    return static_cast<std::uint8_t>(std::max(left, above));
  return static_cast<std::uint8_t>(left + above - upperLeft);
}

} // namespace

// Exposed only to the TLG facade in TlgImage.cpp.
DecodedImage decodeTlg6Image(const std::uint8_t *data, std::size_t size) {
  if (size < 27 || std::memcmp(data, kMagic, sizeof(kMagic)) != 0)
    throw std::runtime_error("KiriKiri: invalid TLG6 header");
  Cursor reader{data, size, 11};
  const auto *flags = reader.take(4);
  const unsigned colors = flags[0];
  if ((colors != 1 && colors != 3 && colors != 4) || flags[1] || flags[2] ||
      flags[3])
    throw std::runtime_error("KiriKiri: unsupported TLG6 color settings");
  const auto width = reader.u32();
  const auto height = reader.u32();
  const auto maxBits = reader.u32();
  if (!width || !height || width > 16384 || height > 16384 ||
      std::uint64_t(width) * height > kMaxPixels)
    throw std::runtime_error("KiriKiri: invalid TLG6 dimensions");
  const std::size_t blocksX = (width + 7u) / 8u;
  const std::size_t blocksY = (height + 7u) / 8u;
  const auto filterLength = reader.u32();
  const auto *filterData = reader.take(filterLength);
  const auto filters =
      decodeFilterTypes(filterData, filterLength, blocksX * blocksY);

  DecodedImage out;
  out.width = width;
  out.height = height;
  out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
  const std::size_t pitch = static_cast<std::size_t>(width) * 4;
  for (std::size_t baseY = 0; baseY < height; baseY += kHeightBlock) {
    const auto rows = std::min<std::size_t>(kHeightBlock, height - baseY);
    const auto pixelCount = rows * width;
    std::vector<std::array<std::uint8_t, 4>> pixels(pixelCount);
    for (unsigned channel = 0; channel < colors; ++channel) {
      const std::uint32_t info = reader.u32();
      const unsigned method = info >> 30;
      const std::size_t bitLength = info & 0x3fffffffu;
      if (maxBits && bitLength > maxBits)
        throw std::runtime_error("KiriKiri: excessive TLG6 bit pool");
      const auto *pool = reader.take((bitLength + 7) / 8);
      if (method == 0) {
        decodeGolomb(pool, bitLength, pixelCount, channel, pixels);
      } else if (method == 3 && bitLength == pixelCount * 8) {
        // Experimental reserved/raw mode: one signed residual byte per
        // sample. The upstream KiriKiri Z loader does not support this
        // method; require the exact expected byte count, never guess.
        for (std::size_t i = 0; i < pixelCount; ++i)
          pixels[i][channel] = pool[i];
      } else {
        throw std::runtime_error(
            "KiriKiri: unsupported TLG6 entropy (Gamma/LZSS or invalid raw)");
      }
    }
    for (std::size_t localY = 0; localY < rows; ++localY) {
      const std::size_t y = baseY + localY;
      auto *line = out.rgba.data() + y * pitch;
      const auto *upper = y ? line - pitch : nullptr;
      for (std::size_t tile = 0; tile < blocksX; ++tile) {
        const auto tileWidth =
            std::min<std::size_t>(kWidthBlock, width - tile * kWidthBlock);
        const auto filter = filters[(baseY / kHeightBlock) * blocksX + tile];
        const auto sourceY = (tile & 1) ? rows - 1 - localY : localY;
        const auto begin = tile * rows * kWidthBlock;
        const auto storageBase = begin + sourceY * tileWidth;
        const bool reverse = (y & 1u) != 0;
        const std::size_t startX = tile * kWidthBlock;
        for (std::size_t localX = 0; localX < tileWidth; ++localX) {
          const std::size_t x = startX + localX;
          const auto sourceX = reverse ? tileWidth - 1 - localX : localX;
          const auto &raw = pixels.at(storageBase + sourceX);
          // One-component TLG6 stores a gray residual rather than three
          // independent color planes.
          const int gray = static_cast<std::int8_t>(raw[0]);
          const auto rgb = colors == 1 ? std::array<int, 3>{gray, gray, gray}
                                       : unfilter(filter, raw);
          for (unsigned channel = 0; channel < 4; ++channel) {
            const int residual = channel == 0   ? rgb[2]
                                 : channel == 1 ? rgb[1]
                                 : channel == 2 ? rgb[0]
                                 : colors == 4
                                     ? static_cast<std::int8_t>(raw[3])
                                     : 0;
            if (channel == 3 && colors != 4) {
              line[x * 4 + channel] = 255;
              continue;
            }
            const unsigned left = x ? line[(x - 1) * 4 + channel]
                                    : (channel == 3 && colors == 3 ? 255u : 0u);
            const unsigned above =
                upper ? upper[x * 4 + channel]
                      : (channel == 3 && colors == 3 ? 255u : 0u);
            const unsigned upperLeft =
                x && upper ? upper[(x - 1) * 4 + channel]
                           : (channel == 3 && colors == 3 ? 255u : 0u);
            line[x * 4 + channel] = static_cast<std::uint8_t>(
                predict(left, above, upperLeft, (filter & 1) != 0) + residual);
          }
        }
      }
    }
  }
  return out;
}

} // namespace Kotonoha::Formats
