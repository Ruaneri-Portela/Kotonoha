#include <SummerRadish/Pack.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
namespace SummerRadish {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
Bytes readRange(const std::filesystem::path &path, size_t offset, size_t size) {
  require(size <= (256u << 20), "CMP entry exceeds allocation limit");
  std::ifstream f(path, std::ios::binary);
  require(bool(f), "Cannot open CMP pack");
  f.seekg(offset);
  Bytes data(size);
  f.read(reinterpret_cast<char *>(data.data()), size);
  require(size_t(f.gcount()) == size, "Truncated CMP pack");
  return data;
}
void appendUtf8(std::string &out, uint32_t c) {
  if (c < 0x80)
    out += char(c);
  else if (c < 0x800) {
    out += char(0xc0 | (c >> 6));
    out += char(0x80 | (c & 63));
  } else if (c < 0x10000) {
    out += char(0xe0 | (c >> 12));
    out += char(0x80 | ((c >> 6) & 63));
    out += char(0x80 | (c & 63));
  } else {
    out += char(0xf0 | (c >> 18));
    out += char(0x80 | ((c >> 12) & 63));
    out += char(0x80 | ((c >> 6) & 63));
    out += char(0x80 | (c & 63));
  }
}
} // namespace
uint32_t word(const Bytes &d, size_t p) {
  require(p <= d.size() && d.size() - p >= 4, "Truncated data word");
  return uint32_t(d[p]) | uint32_t(d[p + 1]) << 8 | uint32_t(d[p + 2]) << 16 |
         uint32_t(d[p + 3]) << 24;
}
Bytes decompress(const Bytes &d, size_t size, size_t p) {
  require(size <= (256u << 20), "CMP decompression exceeds allocation limit");
  Bytes out;
  out.reserve(size);
  while (out.size() < size) {
    require(p < d.size(), "Truncated CMP control");
    unsigned a = d[p++];
    if (a < 128) {
      size_t n = std::min<size_t>(a + 1, size - out.size());
      require(p <= d.size() && n <= d.size() - p, "Truncated CMP literal");
      out.insert(out.end(), d.begin() + p, d.begin() + p + n);
      p += n;
    } else {
      require(p < d.size(), "Truncated CMP reference");
      a = (a << 8) | d[p++];
      size_t n = std::min<size_t>(((a >> 10) & 30) + 2, size - out.size()),
             back = (a & 2047) + 1;
      require(back <= out.size(), "Invalid CMP back reference");
      while (n--)
        out.push_back(out[out.size() - back]);
    }
  }
  return out;
}
std::string utf16(const Bytes &d, size_t p, size_t count) {
  require(p <= d.size() && count <= (d.size() - p) / 2,
          "Truncated UTF-16 string");
  std::string result;
  for (size_t i = 0; i < count; ++i) {
    uint32_t c = d[p + 2 * i] | unsigned(d[p + 2 * i + 1]) << 8;
    if (c >= 0xd800 && c <= 0xdbff) {
      require(++i < count, "Unpaired UTF-16 surrogate");
      uint32_t low = d[p + 2 * i] | unsigned(d[p + 2 * i + 1]) << 8;
      require(low >= 0xdc00 && low <= 0xdfff, "Invalid UTF-16 surrogate");
      c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
    } else
      require(c < 0xdc00 || c > 0xdfff, "Unpaired UTF-16 surrogate");
    appendUtf8(result, c);
  }
  return result;
}
std::string canonical(std::string s) {
  for (char &c : s) {
    if (c == '\\')
      c = '/';
    if (c >= 'a' && c <= 'z')
      c -= 32;
  }
  return s;
}
Pack::Pack(const std::filesystem::path &file) : path(file) {
  const auto length = std::filesystem::file_size(path);
  require(length >= 12 && length <= UINT32_MAX, "Invalid CMP size");
  auto footer = readRange(path, length - 8, 8);
  constexpr std::array<uint8_t, 16> key{0x0f, 0x25, 0x3e, 0x5c, 0x2a, 0x4b,
                                        0x77, 0x90, 0x05, 0x8a, 0x8e, 0x46,
                                        0xeb, 0x3d, 0x11, 0x43};
  for (size_t i = 0; i < 4; ++i)
    require((footer[i] ^ key[i]) == uint8_t("PACK"[i]),
            "Invalid CMP signature/key");
  size_t start = word(footer, 4);
  require(start <= length - 12, "Invalid CMP directory offset");
  auto encoded = readRange(path, start, length - 8 - start);
  size_t raw = word(encoded, 0);
  require(raw <= (16u << 20), "CMP directory too large");
  for (size_t i = 4; i < encoded.size(); ++i)
    encoded[i] ^= key[(i - 4) % 16];
  auto data = decompress(encoded, raw, 4);
  size_t p = 0;
  std::vector<Entry> list;
  while (p < data.size()) {
    require(data.size() - p >= 10, "Truncated CMP directory entry");
    Entry e;
    e.offset = word(data, p);
    size_t units = data[p + 4];
    e.compressed = data[p + 5] != 0;
    e.name = utf16(data, p + 10, units);
    p += 10 + 2 * units;
    require(e.offset <= start, "CMP entry outside data area");
    if (!list.empty()) {
      require(e.offset >= list.back().offset, "Unordered CMP entries");
      list.back().size = e.offset - list.back().offset;
    }
    if (e.name.empty()) {
      require(p == data.size(), "Unexpected CMP sentinel");
      break;
    }
    list.push_back(std::move(e));
  }
  if (!list.empty() && list.back().size == 0)
    list.back().size = start - list.back().offset;
  for (auto &e : list)
    require(directory.emplace(canonical(e.name), e).second,
            "Duplicate CMP entry");
}
bool Pack::contains(const std::string &name) const {
  return directory.count(canonical(name)) != 0;
}
Bytes Pack::read(const std::string &name) const {
  auto it = directory.find(canonical(name));
  if (it == directory.end())
    throw std::runtime_error("CMP asset missing: " + name);
  auto d = readRange(path, it->second.offset, it->second.size);
  return it->second.compressed ? decompress(d, word(d, 0), 4) : d;
}
Image decodeImage(const Bytes &d, const std::string &extension) {
  const auto ext = canonical(extension);
  Image img;
  size_t p = 16;
  if (ext == "TEXB") {
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
  if (ext == "CHR" || ext == "TEXB")
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
  if (ext == "TEXB") {
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

} // namespace SummerRadish
