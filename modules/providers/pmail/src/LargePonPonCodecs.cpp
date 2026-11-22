#include <Pmail/LargePonPonCodecs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Kotonoha::Providers {

namespace {

uint16_t readU16LE(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32LE(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

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

// Standard 88-entry IMA-ADPCM step table (indices 0..87, matching MAIN.exe).
constexpr std::array<int32_t, 88> kImaStepTable = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794};

constexpr std::array<int32_t, 16> kImaIndexTable = {-1, -1, -1, -1, 2, 4, 6, 8,
                                                    -1, -1, -1, -1, 2, 4, 6, 8};

void writeVlq(std::vector<uint8_t> &out, uint32_t value) {
  uint8_t buf[5];
  int count = 0;
  buf[count++] = static_cast<uint8_t>(value & 0x7F);
  value >>= 7;
  while (value > 0) {
    buf[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    value >>= 7;
  }
  for (int i = count - 1; i >= 0; --i)
    out.push_back(buf[i]);
}

struct SmfdEvent {
  uint64_t tick = 0;
  uint8_t status = 0;
  uint8_t d1 = 0;
  uint8_t d2 = 0;
  uint32_t tempoUs = 0; // non-zero for tempo meta event
};

bool parseSmfdEvents(const uint8_t *data, size_t size, uint16_t &timeDiv,
                     std::vector<SmfdEvent> &events, uint64_t &totalTicks) {
  events.clear();
  totalTicks = 0;
  if (!data || size < 4)
    return false;
  timeDiv = readU16LE(data);
  if (timeDiv == 0)
    timeDiv = 480;

  size_t pos = 2;
  uint64_t tick = 0;
  while (pos < size) {
    uint32_t dt = data[pos++];
    if (dt >= 0x80) {
      if (pos >= size)
        break;
      dt = ((dt << 8) | data[pos++]) & 0x7FFF;
    }
    tick += dt;
    if (pos >= size)
      break;
    const uint8_t st = data[pos++];
    if (st >= 0xF2) {
      totalTicks = tick;
      return true;
    }
    if (st == 0xF0) {
      // No-op delay accumulator
      continue;
    }
    if (st == 0xF1) {
      if (pos + 3 > size)
        break;
      const uint8_t hi = data[pos];
      const uint16_t lo = readU16LE(data + pos + 1);
      pos += 3;
      SmfdEvent ev;
      ev.tick = tick;
      ev.status = 0xF1;
      ev.tempoUs = (static_cast<uint32_t>(hi) << 16) | lo;
      events.push_back(ev);
      continue;
    }
    if (st >= 0xC0 && st <= 0xDF) {
      if (pos + 1 > size)
        break;
      SmfdEvent ev;
      ev.tick = tick;
      ev.status = st;
      ev.d1 = data[pos++];
      events.push_back(ev);
      continue;
    }
    if ((st >= 0x80 && st <= 0xBF) || (st >= 0xE0 && st <= 0xEF)) {
      if (pos + 2 > size)
        break;
      SmfdEvent ev;
      ev.tick = tick;
      ev.status = st;
      ev.d1 = data[pos++];
      ev.d2 = data[pos++];
      events.push_back(ev);
      continue;
    }
    // Unexpected byte
    break;
  }
  totalTicks = tick;
  return !events.empty();
}

} // namespace

SurfacePtr decodeCrgbOrBgd(const uint8_t *data, size_t size) {
  if (!data || size == 0)
    return nullptr;

  constexpr int kWidth = 800;
  constexpr int kHeight = 600;
  constexpr size_t kTotalPixels = static_cast<size_t>(kWidth) * kHeight;

  SurfacePtr surface(
      SDL_CreateSurface(kWidth, kHeight, SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    return nullptr;

  auto *dstPixels = static_cast<uint8_t *>(surface->pixels);
  const int pitch = surface->pitch;
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

SurfacePtr decodeCharStream(const uint8_t *data, size_t size, int width,
                            int height) {
  if (!data || size == 0 || width <= 0 || height <= 0)
    return nullptr;

  SurfacePtr surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    return nullptr;

  auto *dstPixels = static_cast<uint8_t *>(surface->pixels);
  const int pitch = surface->pitch;
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

std::vector<SurfacePtr> decodePchrOrIcon(const uint8_t *data, size_t size,
                                         int width, int height) {
  std::vector<SurfacePtr> frames;
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
      frames.push_back(nullptr);
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

std::vector<SurfacePtr> decodePrgb(const uint8_t *data, size_t size) {
  std::vector<SurfacePtr> sprites;
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
      sprites.push_back(nullptr);
      continue;
    }
    const uint16_t w = readU16LE(data + off);
    const uint16_t h = readU16LE(data + off + 2);
    const size_t pixelBytes = static_cast<size_t>(w) * h * 3;
    if (w == 0 || h == 0 || off + 4 + pixelBytes > size) {
      sprites.push_back(nullptr);
      continue;
    }
    SurfacePtr s(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
    if (!s) {
      sprites.push_back(nullptr);
      continue;
    }
    const uint8_t *src = data + off + 4;
    auto *dst = static_cast<uint8_t *>(s->pixels);
    for (int y = 0; y < h; ++y) {
      uint8_t *row = dst + y * s->pitch;
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

SurfacePtr decodeP256Thumbnail(const uint8_t *data, size_t size, size_t index,
                               bool color) {
  constexpr size_t kStride = 0x45A0;
  constexpr int kWidth = 150;
  constexpr int kHeight = 112;
  if (!data || (index + 1) * kStride > size)
    return nullptr;

  const uint8_t *rec = data + index * kStride;
  const uint8_t *grayPal = rec;
  const uint8_t *bgrPal = rec + 0x100;
  const uint8_t *indices = rec + 0x400;

  SurfacePtr s(SDL_CreateSurface(kWidth, kHeight, SDL_PIXELFORMAT_RGBA32));
  if (!s)
    return nullptr;

  auto *dst = static_cast<uint8_t *>(s->pixels);
  for (int y = 0; y < kHeight; ++y) {
    uint8_t *row = dst + y * s->pitch;
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
      SurfacePtr s(
          SDL_CreateSurface(slot.width, slot.height, SDL_PIXELFORMAT_RGBA32));
      if (!s)
        break;
      const uint8_t *src = data + off;
      auto *dst = static_cast<uint8_t *>(s->pixels);
      uint8_t maxAlpha = 0;
      const size_t pixelCount = static_cast<size_t>(slot.width) * slot.height;
      for (size_t p = 0; p < pixelCount; ++p)
        maxAlpha = std::max(maxAlpha, src[p * 4 + 3]);
      // Bottom-up 32-bit BGRA -> top-down RGBA32
      for (int srcY = 0; srcY < slot.height; ++srcY) {
        const int dstY = (slot.height - 1) - srcY;
        uint8_t *row = dst + dstY * s->pitch;
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

SurfacePtr alphaMaskToSurface(const std::vector<uint8_t> &mask, int width,
                              int height) {
  if (mask.size() < static_cast<size_t>(width) * height || width <= 0 ||
      height <= 0) {
    return nullptr;
  }
  SurfacePtr s(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
  if (!s)
    return nullptr;
  auto *dst = static_cast<uint8_t *>(s->pixels);
  for (int y = 0; y < height; ++y) {
    uint8_t *row = dst + y * s->pitch;
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

Adp4Decoded decodeAdp4(const uint8_t *data, size_t size) {
  Adp4Decoded out;
  if (!data || size < 4)
    return out;

  const uint32_t totalPairs = readU32LE(data);
  if (totalPairs == 0 || totalPairs > 48000000)
    return out;

  out.samples.reserve(static_cast<size_t>(totalPairs) * 2);

  int32_t predictor = 0;
  int32_t stepIndex = 0;
  uint32_t bitBuf = 0;
  uint8_t xorKey = 0;
  uint32_t pairsDone = 0;
  size_t pos = 4;

  auto decodeNibble = [&](uint8_t nib) {
    int32_t diff = ((2 * (nib & 7) + 1) * kImaStepTable[stepIndex]) >> 3;
    if (nib & 8)
      diff = -diff;
    predictor = std::clamp(predictor + diff, -32767, 32767);
    stepIndex = std::clamp(stepIndex + kImaIndexTable[nib], 0, 87);
    out.samples.push_back(static_cast<int16_t>(predictor));
  };

  while (pairsDone < totalPairs && pos < size) {
    if (bitBuf < 0x100) {
      bitBuf = static_cast<uint32_t>(data[pos++]) + 0x8000u;
    }
    const uint32_t flag = bitBuf & 1u;
    bitBuf >>= 1;

    if (pos >= size)
      break;
    const uint8_t b1 = data[pos++];
    uint32_t count = 0;
    if (b1 >= 0x80) {
      count = static_cast<uint32_t>(b1 & 0x7F) + 1;
    } else {
      if (pos >= size)
        break;
      const uint8_t b2 = data[pos++];
      count = ((static_cast<uint32_t>(b1) << 8) | b2) + 1;
    }

    const uint32_t remaining = totalPairs - pairsDone;
    const uint32_t actualCount = std::min(count, remaining);
    pairsDone += actualCount;

    if (flag == 1) {
      predictor = 0;
      stepIndex = 0;
      out.samples.insert(out.samples.end(),
                         static_cast<size_t>(actualCount) * 2, int16_t{0});
    } else {
      for (uint32_t i = 0; i < actualCount && pos < size; ++i) {
        const uint8_t byte = static_cast<uint8_t>(data[pos++] ^ xorKey++);
        decodeNibble(byte & 0x0F);
        decodeNibble((byte >> 4) & 0x0F);
      }
    }
  }

  // Compute lip-sync mouth frames every 2205 samples (0.1s @ 22050 Hz).
  constexpr size_t kLipStep = 2205;
  out.mouthFrames.reserve((out.samples.size() + kLipStep - 1) / kLipStep);
  for (size_t i = 0; i < out.samples.size(); i += kLipStep) {
    const int32_t s = out.samples[i];
    out.mouthFrames.push_back((s > 64 || s < -64) ? 0xFF : 0x00);
  }
  return out;
}

std::vector<uint8_t> smfdToStandardMidi(const uint8_t *data, size_t size) {
  uint16_t timeDiv = 480;
  std::vector<SmfdEvent> events;
  uint64_t totalTicks = 0;
  if (!parseSmfdEvents(data, size, timeDiv, events, totalTicks))
    return {};

  std::vector<uint8_t> trk;
  trk.reserve(size + 64);

  // GM System On SysEx at tick 0: F0 05 7E 7F 09 01 F7
  trk.push_back(0x00);
  trk.push_back(0xF0);
  trk.push_back(0x05);
  trk.push_back(0x7E);
  trk.push_back(0x7F);
  trk.push_back(0x09);
  trk.push_back(0x01);
  trk.push_back(0xF7);

  uint64_t prevTick = 0;
  for (const auto &ev : events) {
    const uint32_t delta = static_cast<uint32_t>(ev.tick - prevTick);
    prevTick = ev.tick;
    writeVlq(trk, delta);
    if (ev.status == 0xF1) {
      trk.push_back(0xFF);
      trk.push_back(0x51);
      trk.push_back(0x03);
      trk.push_back(static_cast<uint8_t>((ev.tempoUs >> 16) & 0xFF));
      trk.push_back(static_cast<uint8_t>((ev.tempoUs >> 8) & 0xFF));
      trk.push_back(static_cast<uint8_t>(ev.tempoUs & 0xFF));
    } else if (ev.status >= 0xC0 && ev.status <= 0xDF) {
      trk.push_back(ev.status);
      trk.push_back(ev.d1);
    } else {
      trk.push_back(ev.status);
      trk.push_back(ev.d1);
      trk.push_back(ev.d2);
    }
  }

  // End of track
  writeVlq(trk, static_cast<uint32_t>(
                    totalTicks > prevTick ? totalTicks - prevTick : 0));
  trk.push_back(0xFF);
  trk.push_back(0x2F);
  trk.push_back(0x00);

  std::vector<uint8_t> smf;
  smf.reserve(14 + 8 + trk.size());
  // MThd
  smf.insert(smf.end(), {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                         static_cast<uint8_t>((timeDiv >> 8) & 0xFF),
                         static_cast<uint8_t>(timeDiv & 0xFF)});
  // MTrk
  const uint32_t trkLen = static_cast<uint32_t>(trk.size());
  smf.insert(smf.end(),
             {'M', 'T', 'r', 'k', static_cast<uint8_t>((trkLen >> 24) & 0xFF),
              static_cast<uint8_t>((trkLen >> 16) & 0xFF),
              static_cast<uint8_t>((trkLen >> 8) & 0xFF),
              static_cast<uint8_t>(trkLen & 0xFF)});
  smf.insert(smf.end(), trk.begin(), trk.end());
  return smf;
}

AudioClip decodeSmfdToPcm(const uint8_t *data, size_t size, int sampleRate) {
  AudioClip clip;
  uint16_t timeDiv = 480;
  std::vector<SmfdEvent> events;
  uint64_t totalTicks = 0;
  if (!parseSmfdEvents(data, size, timeDiv, events, totalTicks) ||
      sampleRate <= 0) {
    return clip;
  }

  // Convert event ticks into sample positions using tempo map.
  struct TimedEvent {
    size_t samplePos = 0;
    SmfdEvent ev;
  };
  std::vector<TimedEvent> timed;
  timed.reserve(events.size());

  double curTempoUs = 500000.0;
  uint64_t lastTick = 0;
  double curSample = 0.0;

  for (const auto &ev : events) {
    const uint64_t dt = ev.tick - lastTick;
    curSample += (static_cast<double>(dt) * curTempoUs / 1000000.0) *
                 (static_cast<double>(sampleRate) / timeDiv);
    lastTick = ev.tick;
    if (ev.status == 0xF1 && ev.tempoUs > 0) {
      curTempoUs = static_cast<double>(ev.tempoUs);
    } else {
      timed.push_back({static_cast<size_t>(curSample), ev});
    }
  }
  if (totalTicks > lastTick) {
    curSample +=
        (static_cast<double>(totalTicks - lastTick) * curTempoUs / 1000000.0) *
        (static_cast<double>(sampleRate) / timeDiv);
  }

  // Cap single-pass synthesis at 10 minutes to guard against corrupt files.
  const size_t totalFrames =
      std::min<size_t>(static_cast<size_t>(curSample) + sampleRate / 2,
                       static_cast<size_t>(sampleRate) * 600);
  if (totalFrames == 0)
    return clip;

  struct ChannelState {
    uint8_t program = 0;
    float volume = 100.0f / 127.0f;
    float expression = 1.0f;
    float pan = 0.5f; // 0 = L, 1 = R
    float pitchBendSemis = 0.0f;
  };
  std::array<ChannelState, 16> ch{};

  struct Voice {
    bool active = false;
    bool released = false;
    bool percussion = false;
    uint8_t channel = 0;
    uint8_t note = 0;
    uint8_t program = 0;
    float vel = 0.0f;
    float phase = 0.0f;
    float env = 0.0f;
    float attackRate = 0.02f;
    float decayRate = 0.0002f;
    float sustain = 0.5f;
    float releaseRate = 0.002f;
    uint32_t rng = 0x12345678u;
  };
  constexpr size_t kMaxVoices = 32;
  std::array<Voice, kMaxVoices> voices{};

  auto noteFreq = [](float midiNote) -> float {
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
  };

  auto triggerNote = [&](uint8_t channel, uint8_t note, uint8_t vel) {
    if (vel == 0) {
      for (auto &v : voices) {
        if (v.active && v.channel == channel && v.note == note)
          v.released = true;
      }
      return;
    }
    Voice *slot = nullptr;
    for (auto &v : voices) {
      if (!v.active) {
        slot = &v;
        break;
      }
    }
    if (!slot) {
      // Steal quietest voice
      slot = &voices[0];
      for (auto &v : voices) {
        if (v.env < slot->env)
          slot = &v;
      }
    }
    Voice v{};
    v.active = true;
    v.released = false;
    v.percussion = (channel == 9);
    v.channel = channel;
    v.note = note;
    v.program = ch[channel].program;
    v.vel = static_cast<float>(vel) / 127.0f;
    v.phase = 0.0f;
    v.env = 0.001f;
    v.rng = 0x9E3779B9u ^ (static_cast<uint32_t>(note) << 16) ^
            static_cast<uint32_t>(vel);

    if (v.percussion) {
      v.attackRate = 0.5f;
      v.decayRate = 0.003f;
      v.sustain = 0.0f;
      v.releaseRate = 0.01f;
    } else {
      const uint8_t family = v.program / 8;
      switch (family) {
      case 0: // Piano
      case 1: // Chromatic Percussion
      case 3: // Guitar
        v.attackRate = 0.08f;
        v.decayRate = 0.00018f;
        v.sustain = 0.25f;
        v.releaseRate = 0.0015f;
        break;
      case 2:  // Organ
      case 5:  // Strings
      case 6:  // Ensemble
      case 11: // Synth Pad
        v.attackRate = 0.008f;
        v.decayRate = 0.00005f;
        v.sustain = 0.75f;
        v.releaseRate = 0.0008f;
        break;
      case 4: // Bass
        v.attackRate = 0.06f;
        v.decayRate = 0.00025f;
        v.sustain = 0.45f;
        v.releaseRate = 0.003f;
        break;
      default: // Brass, Reed, Pipe, Synth Lead
        v.attackRate = 0.03f;
        v.decayRate = 0.00012f;
        v.sustain = 0.6f;
        v.releaseRate = 0.002f;
        break;
      }
    }
    *slot = v;
  };

  std::vector<int16_t> pcm(totalFrames * 2, 0);
  size_t evIdx = 0;
  constexpr float kTwoPi = 6.28318530718f;
  const float invRate = 1.0f / static_cast<float>(sampleRate);

  // Process in small 64-frame blocks for speed.
  for (size_t frame = 0; frame < totalFrames; ++frame) {
    while (evIdx < timed.size() && timed[evIdx].samplePos <= frame) {
      const auto &ev = timed[evIdx++].ev;
      const uint8_t type = ev.status & 0xF0;
      const uint8_t c = ev.status & 0x0F;
      if (type == 0x90) {
        triggerNote(c, ev.d1, ev.d2);
      } else if (type == 0x80) {
        triggerNote(c, ev.d1, 0);
      } else if (type == 0xC0) {
        ch[c].program = ev.d1;
      } else if (type == 0xB0) {
        if (ev.d1 == 7)
          ch[c].volume = static_cast<float>(ev.d2) / 127.0f;
        else if (ev.d1 == 11)
          ch[c].expression = static_cast<float>(ev.d2) / 127.0f;
        else if (ev.d1 == 10)
          ch[c].pan = static_cast<float>(ev.d2) / 127.0f;
        else if (ev.d1 == 120 || ev.d1 == 123) {
          for (auto &v : voices) {
            if (v.active && v.channel == c)
              v.released = true;
          }
        }
      } else if (type == 0xE0) {
        const int bend = (static_cast<int>(ev.d2) << 7) | ev.d1;
        ch[c].pitchBendSemis =
            (static_cast<float>(bend - 8192) / 8192.0f) * 2.0f;
      }
    }

    float mixL = 0.0f;
    float mixR = 0.0f;
    for (auto &v : voices) {
      if (!v.active)
        continue;

      // Envelope update
      if (!v.released) {
        if (v.env < 1.0f && v.attackRate > 0.0f) {
          v.env += v.attackRate;
          if (v.env >= 1.0f) {
            v.env = 1.0f;
            v.attackRate = 0.0f; // switch to decay
          }
        } else {
          v.env = std::max(v.sustain, v.env - v.decayRate);
          if (v.percussion && v.env <= 0.0005f) {
            v.active = false;
            continue;
          }
        }
      } else {
        v.env -= v.releaseRate;
        if (v.env <= 0.0005f) {
          v.active = false;
          continue;
        }
      }

      float sample = 0.0f;
      if (v.percussion) {
        // Simple kick vs noise percussion
        if (v.note == 35 || v.note == 36) {
          const float f = 55.0f + 90.0f * v.env;
          v.phase += f * invRate;
          if (v.phase >= 1.0f)
            v.phase -= 1.0f;
          sample = std::sin(v.phase * kTwoPi);
        } else {
          v.rng = v.rng * 1664525u + 1013904223u;
          sample = (static_cast<float>(static_cast<int32_t>(v.rng)) /
                    2147483648.0f) *
                   0.55f;
        }
      } else {
        const float freq =
            noteFreq(static_cast<float>(v.note) + ch[v.channel].pitchBendSemis);
        v.phase += freq * invRate;
        if (v.phase >= 1.0f)
          v.phase -= 1.0f;

        const uint8_t family = v.program / 8;
        if (family == 4) {
          // Bass: warm sine + triangle
          const float tri = 4.0f * std::fabs(v.phase - 0.5f) - 1.0f;
          sample = 0.6f * std::sin(v.phase * kTwoPi) + 0.4f * tri;
        } else if (family == 5 || family == 6 || family == 7 || family == 10) {
          // Strings / Brass / Lead: soft saw + sine
          const float saw = 2.0f * v.phase - 1.0f;
          sample = 0.45f * saw + 0.55f * std::sin(v.phase * kTwoPi);
        } else {
          // Piano / Organ / General: rich harmonic sine
          sample = 0.7f * std::sin(v.phase * kTwoPi) +
                   0.22f * std::sin(v.phase * kTwoPi * 2.0f) +
                   0.08f * std::sin(v.phase * kTwoPi * 3.0f);
        }
      }

      const auto &cs = ch[v.channel];
      const float amp = v.env * v.vel * cs.volume * cs.expression * 0.18f;
      const float panR = cs.pan;
      const float panL = 1.0f - panR;
      mixL += sample * amp * (0.4f + 0.6f * panL);
      mixR += sample * amp * (0.4f + 0.6f * panR);
    }

    // Soft-clip to prevent harsh distortion
    mixL = std::tanh(mixL);
    mixR = std::tanh(mixR);
    pcm[frame * 2 + 0] = static_cast<int16_t>(mixL * 30000.0f);
    pcm[frame * 2 + 1] = static_cast<int16_t>(mixR * 30000.0f);
  }

  clip.spec.format = SDL_AUDIO_S16;
  clip.spec.channels = 2;
  clip.spec.freq = sampleRate;
  clip.data.resize(pcm.size() * sizeof(int16_t));
  std::memcpy(clip.data.data(), pcm.data(), clip.data.size());
  return clip;
}

} // namespace Kotonoha::Providers
