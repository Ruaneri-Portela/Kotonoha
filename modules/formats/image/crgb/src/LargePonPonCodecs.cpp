#include <Kotonoha/Formats/Crgb.hpp>

#include "../../internal/ByteReader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Kotonoha::Formats::Crgb {
namespace {

using Kotonoha::Formats::readU16LE;
using Kotonoha::Formats::readU32LE;

struct CrgbDelta {
  int8_t dR = 0;
  int8_t dG = 0;
  int8_t dB = 0;
};

const std::array<CrgbDelta, 32768> &crgbDeltaTable() {
  static const auto table = [] {
    std::array<CrgbDelta, 32768> t{};
    for (int u = 0; u < 32768; ++u) {
      int dR = (u >> 10) & 0x1F;
      if (dR > 15)
        dR -= 32;
      int dG = (u >> 5) & 0x1F;
      if (dG > 15)
        dG -= 32;
      int dB = u & 0x1F;
      if (dB > 15)
        dB -= 32;
      t[static_cast<size_t>(u)] = {static_cast<int8_t>(dR),
                                   static_cast<int8_t>(dG),
                                   static_cast<int8_t>(dB)};
    }
    return t;
  }();
  return table;
}
} // namespace

DecodedImage decodeCrgbOrBgd(const uint8_t *data, size_t size) {
  if (!data || size == 0)
    return {};

  constexpr int kWidth = 800;
  constexpr int kHeight = 600;
  constexpr size_t kTotalPixels = static_cast<size_t>(kWidth) * kHeight;

  DecodedImage surface;
  surface.width = kWidth;
  surface.height = kHeight;
  surface.rgba.assign(kTotalPixels * 4, 0);
  const int pitch = kWidth * 4;
  auto *dstPixels = surface.rgba.data();
  const auto &delta = crgbDeltaTable();

  auto emitPixel = [&](size_t pixelIndex, int32_t cur) {
    if (pixelIndex >= kTotalPixels)
      return;
    const int srcY = static_cast<int>(pixelIndex / kWidth);
    const int srcX = static_cast<int>(pixelIndex % kWidth);
    const int dstY = (kHeight - 1) - srcY; // bottom-up DIB -> top-down
    uint8_t *p = dstPixels + dstY * pitch + srcX * 4;
    p[0] = static_cast<uint8_t>((cur >> 16) & 0xFF); // R
    p[1] = static_cast<uint8_t>((cur >> 8) & 0xFF);  // G
    p[2] = static_cast<uint8_t>(cur & 0xFF);         // B
    p[3] = 0xFF;                                     // A
  };

  size_t pos = 0;
  size_t px = 0;
  int32_t cur = 0;

  while (pos < size && px < kTotalPixels) {
    const uint8_t b = data[pos++];
    if (b < 0x80) {
      if (pos >= size)
        break;
      const uint16_t idx =
          static_cast<uint16_t>((static_cast<uint16_t>(b) << 8) | data[pos++]);
      const auto &d = delta[idx];
      const uint8_t r = static_cast<uint8_t>(((cur >> 16) & 0xFF) + d.dR);
      const uint8_t g = static_cast<uint8_t>(((cur >> 8) & 0xFF) + d.dG);
      const uint8_t bl = static_cast<uint8_t>((cur & 0xFF) + d.dB);
      cur = (static_cast<int32_t>(r) << 16) | (static_cast<int32_t>(g) << 8) |
            static_cast<int32_t>(bl);
      emitPixel(px++, cur);
    } else if (b < 0xC0) {
      const int count = (b - 0x80) + 1;
      for (int i = 0; i < count && px < kTotalPixels; ++i)
        emitPixel(px++, cur);
    } else if (b < 0xFF) {
      const int count = (b - 0xC0) + 1;
      for (int i = 0; i < count && px < kTotalPixels; ++i) {
        if (pos + 3 > size)
          return surface;
        const uint8_t blue = data[pos++];
        const uint8_t green = data[pos++];
        const uint8_t red = data[pos++];
        cur = static_cast<int32_t>(blue) | (static_cast<int32_t>(green) << 8) |
              (static_cast<int32_t>(red) << 16);
        emitPixel(px++, cur);
      }
    } else {
      break;
    }
  }
  return surface;
}

DecodedImage decodeCharStream(const uint8_t *data, size_t size, int width,
                              int height) {
  if (!data || size == 0 || width <= 0 || height <= 0)
    return {};

  DecodedImage surface;
  surface.width = static_cast<uint32_t>(width);
  surface.height = static_cast<uint32_t>(height);
  surface.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
  const int pitch = width * 4;
  auto *dstPixels = surface.rgba.data();
  std::memset(dstPixels, 0, static_cast<size_t>(pitch) * height);

  size_t pos = 0;
  for (int srcY = 0; srcY < height && pos < size; ++srcY) {
    const int dstY = (height - 1) - srcY; // bottom-up DIB -> top-down
    uint8_t *row = dstPixels + dstY * pitch;
    int x = 0;
    while (pos < size) {
      const uint8_t cmd = data[pos++];
      if (cmd == 0xFF) {
        break;
      } else if (cmd <= 0x7E) {
        if (pos + 3 > size)
          return surface;
        const uint8_t b = data[pos++];
        const uint8_t g = data[pos++];
        const uint8_t r = data[pos++];
        if (x < width) {
          uint8_t *p = row + x * 4;
          p[0] = r;
          p[1] = g;
          p[2] = b;
          p[3] = static_cast<uint8_t>(
              std::min(255, ((static_cast<int>(cmd) + 1) * 255) / 128));
        }
        ++x;
      } else if (cmd <= 0x9E) {
        if (pos + 3 > size)
          return surface;
        const int count = (cmd - 0x7F) + 1;
        const uint8_t b = data[pos++];
        const uint8_t g = data[pos++];
        const uint8_t r = data[pos++];
        for (int i = 0; i < count; ++i, ++x) {
          if (x < width) {
            uint8_t *p = row + x * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 0xFF;
          }
        }
      } else {
        const int count = (cmd - 0x9F) + 1;
        x += count;
      }
    }
  }
  return surface;
}

std::vector<DecodedImage> decodePchrOrIcon(const uint8_t *data, size_t size,
                                           int width, int height) {
  std::vector<DecodedImage> frames;
  if (!data || size < 4)
    return frames;

  const uint32_t frameCount = readU32LE(data);
  if (frameCount == 0 || frameCount > 4096 ||
      4 + static_cast<size_t>(frameCount) * 4 > size) {
    return frames;
  }

  frames.reserve(frameCount);
  for (uint32_t i = 0; i < frameCount; ++i) {
    const uint32_t off = readU32LE(data + 4 + i * 4);
    if (off >= size) {
      frames.push_back({});
      continue;
    }
    const uint32_t end =
        (i + 1 < frameCount)
            ? std::min<uint32_t>(static_cast<uint32_t>(size),
                                 readU32LE(data + 4 + (i + 1) * 4))
            : static_cast<uint32_t>(size);
    const size_t len = (end > off) ? (end - off) : (size - off);
    frames.push_back(decodeCharStream(data + off, len, width, height));
  }
  return frames;
}

std::vector<DecodedImage> decodePrgb(const uint8_t *data, size_t size) {
  std::vector<DecodedImage> sprites;
  if (!data || size < 4)
    return sprites;

  const uint32_t count = readU32LE(data);
  if (count == 0 || count > 4096 || 4 + static_cast<size_t>(count) * 4 > size) {
    return sprites;
  }

  sprites.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t off = readU32LE(data + 4 + i * 4);
    if (off + 4 > size) {
      sprites.push_back({});
      continue;
    }
    const uint16_t w = readU16LE(data + off);
    const uint16_t h = readU16LE(data + off + 2);
    const size_t pixelBytes = static_cast<size_t>(w) * h * 3;
    if (w == 0 || h == 0 || off + 4 + pixelBytes > size) {
      sprites.push_back({});
      continue;
    }
    DecodedImage s;
    s.width = w;
    s.height = h;
    s.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    const int pitch = static_cast<int>(w) * 4;
    const uint8_t *src = data + off + 4;
    auto *dst = s.rgba.data();
    for (int y = 0; y < h; ++y) {
      uint8_t *row = dst + y * pitch;
      for (int x = 0; x < w; ++x) {
        const uint8_t b = *src++;
        const uint8_t g = *src++;
        const uint8_t r = *src++;
        row[x * 4 + 0] = r;
        row[x * 4 + 1] = g;
        row[x * 4 + 2] = b;
        row[x * 4 + 3] = 0xFF;
      }
    }
    sprites.push_back(std::move(s));
  }
  return sprites;
}

size_t p256ThumbnailCount(size_t size) {
  constexpr size_t kStride = 0x45A0; // 17,824 bytes
  return size / kStride;
}

DecodedImage decodeP256Thumbnail(const uint8_t *data, size_t size, size_t index,
                                 bool color) {
  constexpr size_t kStride = 0x45A0;
  constexpr int kWidth = 150;
  constexpr int kHeight = 112;
  if (!data || (index + 1) * kStride > size)
    return {};

  const uint8_t *rec = data + index * kStride;
  const uint8_t *grayPal = rec;
  const uint8_t *bgrPal = rec + 0x100;
  const uint8_t *indices = rec + 0x400;

  DecodedImage s;
  s.width = kWidth;
  s.height = kHeight;
  s.rgba.assign(static_cast<size_t>(kWidth) * kHeight * 4, 0);
  const int pitch = kWidth * 4;
  auto *dst = s.rgba.data();
  for (int y = 0; y < kHeight; ++y) {
    uint8_t *row = dst + y * pitch;
    for (int x = 0; x < kWidth; ++x) {
      const uint8_t idx = indices[y * kWidth + x];
      if (color) {
        const uint8_t b = bgrPal[idx * 3 + 0];
        const uint8_t g = bgrPal[idx * 3 + 1];
        const uint8_t r = bgrPal[idx * 3 + 2];
        row[x * 4 + 0] = r;
        row[x * 4 + 1] = g;
        row[x * 4 + 2] = b;
      } else {
        const uint8_t v = grayPal[idx];
        row[x * 4 + 0] = v;
        row[x * 4 + 1] = v;
        row[x * 4 + 2] = v;
      }
      row[x * 4 + 3] = 0xFF;
    }
  }
  return s;
}

CanmData decodeCanm(const uint8_t *data, size_t size) {
  CanmData result;
  if (!data || size < 0x40)
    return result;

  for (size_t i = 0; i < 4; ++i) {
    const uint8_t *h = data + i * 16;
    auto &slot = result.slots[i];
    slot.x = readU16LE(h + 0);
    slot.y = readU16LE(h + 2);
    slot.width = readU16LE(h + 4);
    slot.height = readU16LE(h + 6);
    slot.slotType = h[8];
    slot.groupId = h[9];
    slot.frameCount = h[10];
    const uint32_t dataOffset = readU32LE(h + 12);

    if (slot.width == 0 || slot.height == 0 || slot.frameCount == 0)
      continue;

    const size_t frameStride =
        static_cast<size_t>(slot.width) * slot.height * 4;
    slot.frames.reserve(slot.frameCount);

    for (uint8_t f = 0; f < slot.frameCount; ++f) {
      const size_t off = static_cast<size_t>(dataOffset) + f * frameStride;
      if (off + frameStride > size)
        break;
      DecodedImage s;
      s.width = slot.width;
      s.height = slot.height;
      s.rgba.assign(static_cast<size_t>(slot.width) * slot.height * 4, 0);
      const int pitch = static_cast<int>(slot.width) * 4;
      const uint8_t *src = data + off;
      auto *dst = s.rgba.data();
      uint8_t maxAlpha = 0;
      const size_t pixelCount = static_cast<size_t>(slot.width) * slot.height;
      for (size_t p = 0; p < pixelCount; ++p)
        maxAlpha = std::max(maxAlpha, src[p * 4 + 3]);
      // Bottom-up 32-bit BGRA -> top-down RGBA32
      for (int srcY = 0; srcY < slot.height; ++srcY) {
        const int dstY = (slot.height - 1) - srcY;
        uint8_t *row = dst + dstY * pitch;
        const uint8_t *srcRow = src + srcY * slot.width * 4;
        for (int x = 0; x < slot.width; ++x) {
          row[x * 4 + 0] = srcRow[x * 4 + 2]; // R
          row[x * 4 + 1] = srcRow[x * 4 + 1]; // G
          row[x * 4 + 2] = srcRow[x * 4 + 0]; // B
          const uint8_t rawA = srcRow[x * 4 + 3];
          if (maxAlpha == 0)
            row[x * 4 + 3] = 0xFF;
          else if (maxAlpha <= 128)
            row[x * 4 + 3] =
                static_cast<uint8_t>(std::min(255, (int(rawA) * 255) / 128));
          else
            row[x * 4 + 3] = rawA;
        }
      }
      slot.frames.push_back(std::move(s));
    }
  }
  return result;
}

std::vector<uint8_t> decodeAlpfMask(const uint8_t *data, size_t size) {
  constexpr int kSrcW = 200;
  constexpr int kSrcH = 150;
  constexpr int kDstW = 800;
  constexpr int kDstH = 600;
  if (!data || size < static_cast<size_t>(kSrcW) * kSrcH)
    return {};

  std::vector<uint8_t> out(static_cast<size_t>(kDstW) * kDstH);
  for (int y = 0; y < kDstH; ++y) {
    const int sy = y / 4;
    for (int x = 0; x < kDstW; ++x) {
      out[static_cast<size_t>(y) * kDstW + x] =
          data[static_cast<size_t>(sy) * kSrcW + (x / 4)];
    }
  }
  return out;
}

std::vector<uint8_t> decodeAlpmMask(const uint8_t *data, size_t size) {
  constexpr size_t kPixels = 800 * 600;
  if (!data || size < kPixels)
    return {};
  return std::vector<uint8_t>(data, data + kPixels);
}

DecodedImage alphaMaskToImage(const std::vector<uint8_t> &mask, int width,
                              int height) {
  if (mask.size() < static_cast<size_t>(width) * height || width <= 0 ||
      height <= 0) {
    return {};
  }
  DecodedImage s;
  s.width = static_cast<uint32_t>(width);
  s.height = static_cast<uint32_t>(height);
  s.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
  const int pitch = width * 4;
  auto *dst = s.rgba.data();
  for (int y = 0; y < height; ++y) {
    uint8_t *row = dst + y * pitch;
    for (int x = 0; x < width; ++x) {
      const uint8_t v = mask[static_cast<size_t>(y) * width + x];
      row[x * 4 + 0] = v;
      row[x * 4 + 1] = v;
      row[x * 4 + 2] = v;
      row[x * 4 + 3] = 0xFF;
    }
  }
  return s;
}
} // namespace Kotonoha::Formats::Crgb
