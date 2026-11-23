// Summer Radish / MEO image, animation and compositor codecs.
#include <SummerRadish/Images.hpp>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace SummerRadish {
namespace {

void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

} // namespace

Image decodeImage(const Bytes &d, const std::string &extension) {
  const auto ext = canonical(extension);
  Image img;
  if (ext == "TEX") {
    img.width = int(word(d, 0));
    img.height = int(word(d, 4));
    require(img.width > 0 && img.height > 0 && img.width <= 8192 &&
                img.height <= 8192 &&
                size_t(img.width) * img.height <= (32u << 20),
            "Invalid TEX dimensions");
    const size_t pixels = size_t(img.width) * img.height;
    require(d.size() - 8 >= pixels * 4, "Truncated TEX");
    img.rgba.resize(pixels * 4);
    for (size_t i = 0; i < pixels; ++i) {
      img.rgba[i * 4] = d[8 + i * 4 + 2];
      img.rgba[i * 4 + 1] = d[8 + i * 4 + 1];
      img.rgba[i * 4 + 2] = d[8 + i * 4];
      img.rgba[i * 4 + 3] = d[8 + i * 4 + 3];
    }
    return img;
  }
  size_t p = 16;
  if (ext == "TEXB" || ext == "ALP") {
    img.width = int(word(d, 0));
    img.height = int(word(d, 4));
    p = 8;
  } else if (ext == "CHR") {
    img.width = int(word(d, 8));
    img.height = int(word(d, 12));
    img.x = int(word(d, 16));
    img.y = int(word(d, 20));
    p = 32;
  } else {
    img.width = int(word(d, 4));
    img.height = int(word(d, 8));
  }
  require(img.width > 0 && img.height > 0 && img.width <= 8192 &&
              img.height <= 8192 &&
              size_t(img.width) * img.height <= (32u << 20),
          "Invalid picture dimensions");
  const size_t pixels = size_t(img.width) * img.height;
  img.rgba.resize(pixels * 4);
  if (ext == "CHR" || ext == "TEXB" || ext == "ALP")
    img.nativePixels.resize(pixels * 4);
  auto put = [&](size_t index, uint8_t b, uint8_t g, uint8_t r,
                 uint8_t a = 255) {
    require(index < pixels, "Picture output overflow");
    size_t at =
        ((img.height - 1 - index / img.width) * img.width + index % img.width) *
        4;
    img.rgba[at] = r;
    img.rgba[at + 1] = g;
    img.rgba[at + 2] = b;
    img.rgba[at + 3] = a;
  };
  auto putNative = [&](size_t index, uint8_t b, uint8_t g, uint8_t r,
                       uint8_t alpha) {
    require(index < pixels, "Picture output overflow");
    const size_t at =
        ((img.height - 1 - index / img.width) * img.width + index % img.width) *
        4;
    img.nativePixels[at] = r;
    img.nativePixels[at + 1] = g;
    img.nativePixels[at + 2] = b;
    img.nativePixels[at + 3] = alpha;
    if (alpha >= 128)
      put(index, b, g, r);
    else if (alpha == 0)
      put(index, 0, 0, 0, 0);
    else {
      // The generic SDL path is straight alpha. Native composition below keeps
      // the original source values and never performs this lossy conversion.
      auto straight = [&](uint8_t c) {
        return uint8_t(std::min(255u, (unsigned(c) * 128 + alpha / 2) / alpha));
      };
      put(index, straight(b), straight(g), straight(r),
          uint8_t((unsigned(alpha) * 255 + 64) / 128));
    }
  };
  auto byte = [&]() {
    require(p < d.size(), "Truncated picture stream");
    return d[p++];
  };
  if (ext == "ALP") {
    require(d.size() - p >= pixels, "Truncated ALP");
    for (size_t i = 0; i < pixels; ++i) {
      const uint8_t v = byte();
      put(i, v, v, v, v);
      const size_t at =
          ((img.height - 1 - i / img.width) * img.width + i % img.width) * 4;
      img.nativePixels[at] = v;
      img.nativePixels[at + 1] = v;
      img.nativePixels[at + 2] = v;
      img.nativePixels[at + 3] = uint8_t((unsigned(v) * 128) / 255);
    }
  } else if (ext == "TEXB") {
    require(d.size() - p >= pixels * 4, "Truncated TEXB");
    for (size_t i = 0; i < pixels; ++i) {
      uint8_t b = byte(), g = byte(), r = byte(), a = byte();
      putNative(i, b, g, r, a);
    }
  } else if (ext == "BGD") {
    constexpr int delta[3][8] = {{1, 2, 4, 8, 16, 38, 80, 170},
                                 {2, 4, 6, 12, 24, 48, 96, 192},
                                 {5, 10, 20, 30, 50, 80, 130, 210}};
    constexpr int next[3][8] = {{0, 0, 0, 1, 1, 1, 1, 1},
                                {0, 0, 1, 1, 2, 2, 1, 1},
                                {1, 1, 2, 2, 2, 2, 1, 1}};
    int value[3] = {0, 0, 0}, state[3] = {1, 1, 1};
    // Two pixels per three bytes: B0,G0,R0,R1,B1,G1 nibbles.
    for (size_t i = 0; i < pixels; i += 2) {
      uint8_t a = byte(), b = byte(), c = byte();
      int n[6] = {a & 15, a >> 4, b & 15, c & 15, c >> 4, b >> 4};
      for (int j = 0; j < 6; ++j) {
        int channel = j < 3 ? j : (j == 3 ? 0 : j == 4 ? 1 : 2), q = n[j];
        value[channel] =
            (value[channel] + (q < 8 ? 1 : -1) * delta[state[channel]][q & 7]) &
            255;
        state[channel] = next[state[channel]][q & 7];
        if (j == 2)
          put(i, value[0], value[1], value[2]);
      }
      if (i + 1 < pixels)
        put(i + 1, value[0], value[1], value[2]);
    }
  } else if (ext == "CGD") {
    uint8_t b = 0, g = 0, r = 0;
    size_t i = 0;
    auto signed5 = [](unsigned n) { return int(n & 31) - ((n & 16) ? 32 : 0); };
    while (i < pixels) {
      unsigned a = byte();
      if (a < 128) {
        unsigned v = (a << 8) | byte();
        b = uint8_t(b + signed5(v));
        g = uint8_t(g + signed5(v >> 5));
        r = uint8_t(r + signed5(v >> 10));
        put(i++, b, g, r);
      } else if (a < 192) {
        unsigned n = a - 127;
        require(n <= pixels - i, "CGD run overflow");
        while (n--)
          put(i++, b, g, r);
      } else {
        require(a != 255, "CGD ended before all pixels");
        unsigned n = a - 191;
        require(n <= pixels - i, "CGD literal overflow");
        while (n--) {
          b = byte();
          g = byte();
          r = byte();
          put(i++, b, g, r);
        }
      }
    }
  } else if (ext == "CHR") {
    for (int row = 0; row < img.height; ++row) {
      size_t x = 0;
      while (true) {
        unsigned a = byte();
        if (a == 255)
          break;
        if (a < 127) {
          uint8_t b = byte(), g = byte(), r = byte();
          require(x < size_t(img.width), "CHR pixel overflow");
          const size_t index = size_t(row) * img.width + x++;
          // CHR files store straight BGR. GObj_CHRData::FUN_10001040
          // premultiplies the working stream before FUN_100012a0 blends it.
          putNative(index, uint8_t(unsigned(b) * (a + 1) >> 7),
                    uint8_t(unsigned(g) * (a + 1) >> 7),
                    uint8_t(unsigned(r) * (a + 1) >> 7), uint8_t(a + 1));
          // Preserve the original straight RGB in the generic SDL fallback.
          put(index, b, g, r, uint8_t(((a + 1) * 255 + 64) / 128));
        } else if (a < 159) {
          unsigned n = a - 126;
          require(n <= size_t(img.width) - x, "CHR run overflow");
          uint8_t b = byte(), g = byte(), r = byte();
          while (n--)
            putNative(size_t(row) * img.width + x++, b, g, r, 128);
        } else {
          unsigned n = a - 158;
          require(n <= size_t(img.width) - x, "CHR skip overflow");
          x += n;
        }
      }
    }
  } else
    throw std::runtime_error("Unsupported Summer Radish picture: " + extension);
  return img;
}
std::vector<CharacterAnimation> decodeCharacterAnimations(const Bytes &d) {
  require(d.size() >= 32, "Truncated CHR animation header");
  size_t p = word(d, 4);
  require(p >= 32 && p <= d.size(), "Invalid CHR animation offset");
  auto shortAt = [&](size_t offset) {
    require(offset <= d.size() && d.size() - offset >= 2,
            "Truncated CHR animation word");
    return int(int16_t(uint16_t(d[offset]) | (uint16_t(d[offset + 1]) << 8)));
  };
  std::vector<CharacterAnimation> result;
  while (p < d.size()) {
    const size_t size = word(d, p);
    if (size == 0) {
      require(d.size() - p == 4, "Trailing CHR animation bytes");
      break;
    }
    require(size >= 20 && size <= d.size() - p, "Invalid CHR animation record");
    CharacterAnimation animation;
    animation.actor = shortAt(p + 4);
    animation.selector = shortAt(p + 6);
    const int count = shortAt(p + 8), width = shortAt(p + 16),
              height = shortAt(p + 18);
    require(count > 0 && count <= 256 && width > 0 && height > 0 &&
                size_t(width) * height <= ((size - 20) / 4) / size_t(count),
            "Invalid CHR animation frames");
    require(size == 20 + size_t(count) * width * height * 4,
            "CHR animation size mismatch");
    size_t pixelsAt = p + 20;
    for (int frame = 0; frame < count; ++frame) {
      // Embedded SubAnime pixels are straight BGRA with coverage/128; the
      // original loader premultiplies them before invoking Texture's blend.
      Bytes texture;
      texture.reserve(8 + size_t(width) * height * 4);
      for (auto value : {width, height})
        for (int byte = 0; byte < 4; ++byte)
          texture.push_back(uint8_t(uint32_t(value) >> (byte * 8)));
      for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel) {
        const unsigned alpha = d[pixelsAt + 3];
        for (size_t channel = 0; channel < 3; ++channel)
          texture.push_back(
              uint8_t((unsigned(d[pixelsAt + channel]) * alpha) >> 7));
        texture.push_back(uint8_t(alpha));
        pixelsAt += 4;
      }
      Image image = decodeImage(texture, "TEXB");
      image.x = shortAt(p + 12);
      image.y = shortAt(p + 14);
      animation.frames.push_back(std::move(image));
    }
    result.push_back(std::move(animation));
    p += size;
  }
  return result;
}

bool selectCharacterAnimationFrame(std::vector<CharacterAnimation> &animations,
                                   int actor, int selector, int frame) {
  bool selected = false;
  for (auto &animation : animations)
    if (animation.actor == actor && animation.selector == selector &&
        frame >= 0 && size_t(frame) < animation.frames.size()) {
      animation.currentFrame = size_t(frame);
      selected = true;
    }
  return selected;
}

void compositeCharacterAnimations(
    Image &destination, const std::vector<CharacterAnimation> &animations,
    int offsetX, int offsetY) {
  for (const auto &animation : animations) {
    require(animation.currentFrame < animation.frames.size(),
            "Invalid active character animation frame");
    const auto &frame = animation.frames[animation.currentFrame];
    compositeImage(destination, frame, frame.x + offsetX, frame.y + offsetY);
  }
}

void compositeImage(Image &destination, const Image &source, int x, int y) {
  compositeImageRegion(destination, source, x, y, 0, 0, source.width,
                       source.height);
}
void compositeImageRegion(Image &destination, const Image &source, int x, int y,
                          int sourceX, int sourceY, int width, int height) {
  require(destination.width > 0 && destination.height > 0 &&
              destination.rgba.size() ==
                  size_t(destination.width) * destination.height * 4,
          "Invalid compositor destination");
  require(source.width > 0 && source.height > 0 &&
              source.rgba.size() == size_t(source.width) * source.height * 4 &&
              (source.nativePixels.empty() ||
               source.nativePixels.size() == source.rgba.size()),
          "Invalid compositor source");
  require(sourceX >= 0 && sourceY >= 0 && width >= 0 && height >= 0 &&
              int64_t(sourceX) + width <= source.width &&
              int64_t(sourceY) + height <= source.height,
          "Invalid compositor source rectangle");
  const int64_t left = std::max<int64_t>(0, x), top = std::max<int64_t>(0, y);
  const int64_t right =
      std::min<int64_t>(destination.width, int64_t(x) + width);
  const int64_t bottom =
      std::min<int64_t>(destination.height, int64_t(y) + height);
  for (int64_t dy = top; dy < bottom; ++dy)
    for (int64_t dx = left; dx < right; ++dx) {
      const size_t in =
          (size_t(dy - y + sourceY) * source.width + size_t(dx - x + sourceX)) *
          4;
      const size_t out = (size_t(dy) * destination.width + size_t(dx)) * 4;
      if (!source.nativePixels.empty()) {
        const unsigned alpha = source.nativePixels[in + 3];
        if (alpha == 0)
          continue;
        for (size_t channel = 0; channel < 3; ++channel) {
          if (alpha >= 128)
            destination.rgba[out + channel] = source.nativePixels[in + channel];
          else
            destination.rgba[out + channel] =
                uint8_t(source.nativePixels[in + channel] +
                        ((unsigned(destination.rgba[out + channel]) *
                          (256 - 2 * alpha)) >>
                         8));
        }
      } else {
        // BGD/CGD are opaque. Reject other callers rather than silently
        // replacing the original blend by straight-alpha arithmetic.
        require(source.rgba[in + 3] == 255,
                "Compositor requires opaque or native pixels");
        for (size_t channel = 0; channel < 3; ++channel)
          destination.rgba[out + channel] = source.rgba[in + channel];
      }
      destination.rgba[out + 3] = 255;
    }
  // A composed frame is opaque, with no reusable original native source plane.
  destination.nativePixels.clear();
}

namespace {
Image decodeManpuFrame(const Bytes &d, size_t baseOffset, size_t limitOffset) {
  require(limitOffset <= d.size() && baseOffset <= limitOffset &&
              limitOffset - baseOffset >= 32,
          "Truncated MANP frame header");
  Image img;
  img.x = int(int32_t(word(d, baseOffset + 8)));
  img.y = int(int32_t(word(d, baseOffset + 12)));
  img.width = int(word(d, baseOffset + 16));
  img.height = int(word(d, baseOffset + 20));
  require(img.width > 0 && img.height > 0 && img.width <= 4096 &&
              img.height <= 4096,
          "Invalid MANP frame dimensions");
  const size_t pixels = size_t(img.width) * size_t(img.height);
  img.rgba.assign(pixels * 4, 0);
  img.nativePixels.assign(pixels * 4, 0);
  auto put = [&](size_t i, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    const size_t at = ((img.height - 1 - int(i / img.width)) * img.width +
                       int(i % img.width)) *
                      4;
    img.rgba[at] = r;
    img.rgba[at + 1] = g;
    img.rgba[at + 2] = b;
    img.rgba[at + 3] = a;
  };
  auto putNative = [&](size_t i, uint8_t b, uint8_t g, uint8_t r,
                       uint8_t alpha) {
    const size_t at = ((img.height - 1 - int(i / img.width)) * img.width +
                       int(i % img.width)) *
                      4;
    img.nativePixels[at] = r;
    img.nativePixels[at + 1] = g;
    img.nativePixels[at + 2] = b;
    img.nativePixels[at + 3] = alpha;
    if (alpha >= 128)
      put(i, b, g, r, 255);
    else if (alpha == 0)
      put(i, 0, 0, 0, 0);
    else {
      auto straight = [&](uint8_t c) {
        return uint8_t(
            std::min(255u, (unsigned(c) * 128u + alpha / 2u) / alpha));
      };
      put(i, straight(b), straight(g), straight(r),
          uint8_t((unsigned(alpha) * 255u + 64u) / 128u));
    }
  };
  size_t p = baseOffset + 32;
  auto byte = [&]() {
    require(p < limitOffset, "Truncated MANP frame stream");
    return d[p++];
  };
  for (int row = 0; row < img.height; ++row) {
    size_t x = 0;
    while (true) {
      unsigned a = byte();
      if (a == 255)
        break;
      if (a < 127) {
        uint8_t b = byte(), g = byte(), r = byte();
        require(x < size_t(img.width), "MANP pixel overflow");
        // MANP frames store premultiplied BGR on disk (GObj_Manpu.mod);
        // do NOT premultiply again.
        putNative(size_t(row) * img.width + x++, b, g, r, uint8_t(a + 1));
      } else if (a < 159) {
        unsigned n = a - 126;
        require(n <= size_t(img.width) - x, "MANP run overflow");
        uint8_t b = byte(), g = byte(), r = byte();
        while (n--)
          putNative(size_t(row) * img.width + x++, b, g, r, 128);
      } else {
        unsigned n = a - 158;
        require(n <= size_t(img.width) - x, "MANP skip overflow");
        x += n;
      }
    }
  }
  return img;
}
} // namespace

ManpuAnimation decodeManpu(const Bytes &d) {
  ManpuAnimation result;
  size_t p = 0;
  while (p + 8 <= d.size()) {
    const char *fourcc = reinterpret_cast<const char *>(d.data() + p);
    const uint32_t chunkSize = word(d, p + 4);
    const size_t payloadStart = p + 8;
    require(chunkSize <= d.size() - payloadStart, "Truncated MANP chunk");
    const size_t payloadEnd = payloadStart + chunkSize;
    if (std::memcmp(fourcc, "PICT", 4) == 0) {
      size_t tp = payloadStart;
      std::vector<std::pair<int32_t, uint32_t>> table;
      while (tp + 4 <= payloadEnd) {
        const int32_t id = int32_t(word(d, tp));
        tp += 4;
        if (id == -1)
          break;
        require(tp + 4 <= payloadEnd, "Truncated MANP PICT table");
        const uint32_t rel = word(d, tp);
        tp += 4;
        table.emplace_back(id, rel);
      }
      for (const auto &[id, rel] : table) {
        require(rel <= chunkSize, "Invalid MANP PICT frame offset");
        result.frames.emplace(
            id, decodeManpuFrame(d, payloadStart + rel, payloadEnd));
      }
    } else if (std::memcmp(fourcc, "SEQS", 4) == 0) {
      size_t tp = payloadStart;
      std::vector<std::pair<int32_t, uint32_t>> table;
      while (tp + 4 <= payloadEnd) {
        const int32_t id = int32_t(word(d, tp));
        tp += 4;
        if (id == -1)
          break;
        require(tp + 4 <= payloadEnd, "Truncated MANP SEQS table");
        const uint32_t rel = word(d, tp);
        tp += 4;
        table.emplace_back(id, rel);
      }
      for (size_t i = 0; i < table.size(); ++i) {
        const uint32_t rel = table[i].second;
        require(rel <= chunkSize, "Invalid MANP SEQS offset");
        size_t sp = payloadStart + rel;
        std::vector<uint32_t> ops;
        while (sp + 4 <= payloadEnd) {
          const uint32_t w = word(d, sp);
          sp += 4;
          ops.push_back(w);
          if ((w >> 24) == 0x00)
            break;
        }
        result.sequences.emplace(table[i].first, std::move(ops));
      }
    } else if (std::memcmp(fourcc, "ENDE", 4) == 0) {
      break;
    }
    p = payloadEnd;
  }
  return result;
}

ClickableMap decodeClickableMap(const Bytes &d) {
  require(d.size() >= 8, "Truncated CMAP header");
  ClickableMap map;
  map.width = int(word(d, 0));
  map.height = int(word(d, 4));
  require(map.width > 0 && map.height > 0 && map.width <= 4096 &&
              map.height <= 4096,
          "Invalid CMAP dimensions");
  const size_t pixels = size_t(map.width) * size_t(map.height);
  require(d.size() - 8 >= pixels, "Truncated CMAP pixel table");
  map.hitIds.assign(d.begin() + 8, d.begin() + 8 + pixels);
  return map;
}

void applyLightingTint(Image &image, int tintMode) {
  if (tintMode == 0)
    return;
  // 1 = evening (1.0, 0.9, 0.9), 2 = night (0.7, 0.7, 1.0)
  const float rScale = (tintMode == 2) ? 0.7f : 1.0f;
  const float gScale = (tintMode == 1) ? 0.9f : ((tintMode == 2) ? 0.7f : 1.0f);
  const float bScale = (tintMode == 1) ? 0.9f : 1.0f;
  auto scalePlane = [&](Bytes &plane) {
    for (size_t i = 0; i + 4 <= plane.size(); i += 4) {
      plane[i] = uint8_t(std::min(255.0f, plane[i] * rScale));
      plane[i + 1] = uint8_t(std::min(255.0f, plane[i + 1] * gScale));
      plane[i + 2] = uint8_t(std::min(255.0f, plane[i + 2] * bScale));
    }
  };
  scalePlane(image.rgba);
  if (!image.nativePixels.empty())
    scalePlane(image.nativePixels);
}

} // namespace SummerRadish
