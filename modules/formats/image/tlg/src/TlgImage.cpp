// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
// Original bounds-checked implementation of the published TLG5 format.
#include <Kotonoha/Formats/Tlg.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace Kotonoha::Formats {
// Implemented in Tlg6Image.cpp; this facade handles TLG0 and all signatures.
DecodedImage decodeTlg6Image(const std::uint8_t *data, std::size_t size);
namespace {

constexpr std::array<std::uint8_t, 11> kTlg0{'T', 'L', 'G', '0', '.', '0',
                                             0,   's', 'd', 's', 0x1a};
constexpr std::array<std::uint8_t, 11> kTlg5{'T', 'L', 'G', '5', '.', '0',
                                             0,   'r', 'a', 'w', 0x1a};
constexpr std::array<std::uint8_t, 11> kTlg6{'T', 'L', 'G', '6', '.', '0',
                                             0,   'r', 'a', 'w', 0x1a};

bool signature(const std::uint8_t *bytes, std::size_t size,
               const std::array<std::uint8_t, 11> &magic) {
  return size >= magic.size() &&
         std::memcmp(bytes, magic.data(), magic.size()) == 0;
}

struct Reader {
  const std::uint8_t *data;
  std::size_t size;
  std::size_t cursor = 0;

  void need(std::size_t n) const {
    if (cursor > size || n > size - cursor)
      throw std::runtime_error("KiriKiri: truncated TLG5 image");
  }
  std::uint8_t byte() {
    need(1);
    return data[cursor++];
  }
  std::uint32_t u32() {
    need(4);
    const std::uint32_t value = std::uint32_t(data[cursor]) |
                                (std::uint32_t(data[cursor + 1]) << 8) |
                                (std::uint32_t(data[cursor + 2]) << 16) |
                                (std::uint32_t(data[cursor + 3]) << 24);
    cursor += 4;
    return value;
  }
  const std::uint8_t *take(std::size_t count) {
    need(count);
    const auto *result = data + cursor;
    cursor += count;
    return result;
  }
};

// TLG5's per-channel sliding dictionary persists across blocks and color
// components. A copy may refer to dictionary bytes just written by itself.
std::vector<std::uint8_t>
decodeSlide(const std::uint8_t *src, std::size_t size, std::size_t expected,
            std::array<std::uint8_t, 4096> &dictionary,
            std::size_t &dictionaryPos) {
  std::vector<std::uint8_t> output;
  output.reserve(expected);
  std::size_t pos = 0;
  unsigned flagBits = 0;
  std::uint8_t flags = 0;
  const auto append = [&](std::uint8_t value) {
    if (output.size() >= expected)
      throw std::runtime_error("KiriKiri: TLG5 LZSS expands beyond scanline");
    output.push_back(value);
    dictionary[dictionaryPos] = value;
    dictionaryPos = (dictionaryPos + 1) & 4095u;
  };
  while (pos < size) {
    if (flagBits == 0) {
      flags = src[pos++];
      flagBits = 8;
    }
    if (flags & 1u) {
      if (size - pos < 2)
        throw std::runtime_error("KiriKiri: truncated TLG5 LZSS reference");
      const auto b0 = src[pos++];
      const auto b1 = src[pos++];
      std::size_t copyAt = b0 | (std::size_t(b1 & 0x0f) << 8);
      std::size_t length = (b1 >> 4) + 3;
      if (length == 18) {
        if (pos == size)
          throw std::runtime_error("KiriKiri: truncated TLG5 LZSS length");
        length += src[pos++];
      }
      if (length > expected - output.size())
        throw std::runtime_error("KiriKiri: oversized TLG5 LZSS run");
      while (length--) {
        const auto value = dictionary[copyAt];
        copyAt = (copyAt + 1) & 4095u;
        append(value);
      }
    } else {
      if (pos == size)
        throw std::runtime_error("KiriKiri: truncated TLG5 LZSS literal");
      append(src[pos++]);
    }
    flags >>= 1;
    --flagBits;
  }
  if (output.size() != expected)
    throw std::runtime_error("KiriKiri: incomplete TLG5 LZSS plane");
  return output;
}

DecodedImage decodeTlg5(const std::uint8_t *data, std::size_t size) {
  if (!signature(data, size, kTlg5))
    throw std::runtime_error(
        "KiriKiri: only TLG5 image decoding is implemented");
  Reader reader{data, size, 11};
  const auto colors = reader.byte();
  const auto width = reader.u32();
  const auto height = reader.u32();
  const auto blockHeight = reader.u32();
  constexpr std::uint64_t kMaxPixels = 32ull * 1024 * 1024;
  if ((colors != 3 && colors != 4) || !width || !height || !blockHeight ||
      width > 16384 || height > 16384 ||
      std::uint64_t(width) * height > kMaxPixels)
    throw std::runtime_error("KiriKiri: invalid TLG5 image dimensions");

  const auto blocks = (std::uint64_t(height) + blockHeight - 1) / blockHeight;
  reader.take(static_cast<std::size_t>(blocks * 4)); // block size hints
  DecodedImage out;
  out.width = width;
  out.height = height;
  out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
  const std::size_t pitch = static_cast<std::size_t>(width) * 4;

  std::array<std::uint8_t, 4096> dictionary{};
  std::size_t dictionaryPos = 0;
  std::array<std::vector<std::uint8_t>, 4> planes;
  for (std::uint32_t baseY = 0; baseY < height;) {
    const auto rows = std::min(blockHeight, height - baseY);
    const auto planeSize = static_cast<std::size_t>(width) * rows;
    for (unsigned c = 0; c < colors; ++c) {
      const auto method = reader.byte();
      const auto storedSize = reader.u32();
      const auto *packed = reader.take(storedSize);
      if (method == 0) {
        planes[c] = decodeSlide(packed, storedSize, planeSize, dictionary,
                                dictionaryPos);
      } else if (method != 0 && storedSize == planeSize) {
        planes[c].assign(packed, packed + storedSize);
      } else {
        throw std::runtime_error("KiriKiri: invalid TLG5 plane encoding");
      }
    }
    for (std::uint32_t localY = 0; localY < rows; ++localY) {
      const auto y = baseY + localY;
      auto *line = out.rgba.data() + std::size_t(y) * pitch;
      const auto *previous = y ? line - pitch : nullptr;
      std::array<std::uint8_t, 4> horizontal{};
      for (std::uint32_t x = 0; x < width; ++x) {
        const auto offset = static_cast<std::size_t>(localY) * width + x;
        const auto g = planes[1][offset];
        const auto b = static_cast<std::uint8_t>(planes[0][offset] + g);
        const auto r = static_cast<std::uint8_t>(planes[2][offset] + g);
        horizontal[0] = static_cast<std::uint8_t>(horizontal[0] + r);
        horizontal[1] = static_cast<std::uint8_t>(horizontal[1] + g);
        horizontal[2] = static_cast<std::uint8_t>(horizontal[2] + b);
        if (colors == 4)
          horizontal[3] =
              static_cast<std::uint8_t>(horizontal[3] + planes[3][offset]);
        for (unsigned channel = 0; channel < 3; ++channel) {
          const auto above = previous ? previous[x * 4 + channel] : 0u;
          line[x * 4 + channel] =
              static_cast<std::uint8_t>(horizontal[channel] + above);
        }
        line[x * 4 + 3] =
            colors == 4
                ? static_cast<std::uint8_t>(
                      horizontal[3] + (previous ? previous[x * 4 + 3] : 0u))
                : 255;
      }
    }
    baseY += rows;
  }
  return out;
}

} // namespace

bool isTlgImage(const std::vector<std::uint8_t> &bytes) {
  return signature(bytes.data(), bytes.size(), kTlg0) ||
         signature(bytes.data(), bytes.size(), kTlg5) ||
         signature(bytes.data(), bytes.size(), kTlg6);
}

DecodedImage decodeTlgImage(const std::vector<std::uint8_t> &bytes) {
  const auto *data = bytes.data();
  auto size = bytes.size();
  if (signature(data, size, kTlg0)) {
    Reader wrapper{data, size, 11};
    const auto length = wrapper.u32();
    data = wrapper.take(length);
    size = length;
  }
  if (signature(data, size, kTlg6))
    return decodeTlg6Image(data, size);
  return decodeTlg5(data, size);
}

} // namespace Kotonoha::Formats
