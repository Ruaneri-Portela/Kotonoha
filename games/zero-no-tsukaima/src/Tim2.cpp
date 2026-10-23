#include <ZeroNoTsukaima/Tim2.hpp>

#include <algorithm>
#include <cstring>

namespace ZeroNoTsukaima {

namespace {

uint32_t U32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t U16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint8_t PsAlpha(uint8_t a) { return static_cast<uint8_t>(std::min(255, a * 2)); }

void ReadColour(const uint8_t *p, int bytes, uint8_t *rgba) {
  if (bytes == 2) {
    const uint16_t v = U16(p);
    rgba[0] = static_cast<uint8_t>(((v >> 0) & 31) * 255 / 31);
    rgba[1] = static_cast<uint8_t>(((v >> 5) & 31) * 255 / 31);
    rgba[2] = static_cast<uint8_t>(((v >> 10) & 31) * 255 / 31);
    rgba[3] = (v & 0x8000) ? 255 : 0;
  } else if (bytes == 3) {
    rgba[0] = p[0], rgba[1] = p[1], rgba[2] = p[2], rgba[3] = 255;
  } else {
    rgba[0] = p[0], rgba[1] = p[1], rgba[2] = p[2], rgba[3] = PsAlpha(p[3]);
  }
}

int BytesFor(int type) { return type == 1 ? 2 : type == 2 ? 3 : 4; }

} // namespace

bool DecodeTim2(const uint8_t *data, size_t size, Image *out) {
  if (size < 64 || std::memcmp(data, "TIM2", 4) != 0) return false;
  const uint8_t *pic = data + 16;
  const uint32_t clutSize = U32(pic + 4), imageSize = U32(pic + 8);
  const uint16_t headerSize = U16(pic + 12), clutColors = U16(pic + 14);
  const uint8_t cType = pic[18];
  const uint8_t type = pic[19];
  const int width = U16(pic + 20), height = U16(pic + 22);
  if (width <= 0 || height <= 0) return false;
  if (static_cast<size_t>(16) + headerSize + imageSize + clutSize > size) return false;

  const uint8_t *image = pic + headerSize;
  const uint8_t *clut = image + imageSize;
  out->width = width;
  out->height = height;
  out->rgba.assign(static_cast<size_t>(width) * height * 4, 0);

  if (type == 4 || type == 5) {
    const int count = type == 5 ? 256 : 16;
    const int cBytes = BytesFor(cType & 0x3F);
    if (clutColors < static_cast<unsigned>(count) &&
        clutSize < static_cast<uint32_t>(count * cBytes)) {
      return false;
    }
    std::vector<uint8_t> palette(static_cast<size_t>(count) * 4);
    for (int i = 0; i < count; ++i) {
      int src = i;
      // CSM1 8-bit palettes interleave 8-entry blocks.
      if (type == 5 && (cType & 0x80) == 0) src = (i & ~0x18) | ((i & 8) << 1) | ((i & 16) >> 1);
      ReadColour(clut + static_cast<size_t>(src) * cBytes, cBytes, &palette[i * 4]);
    }
    for (int i = 0; i < width * height; ++i) {
      const int index = type == 5 ? image[i]
                                  : ((image[i / 2] >> ((i & 1) * 4)) & 15);
      std::memcpy(&out->rgba[static_cast<size_t>(i) * 4], &palette[index * 4], 4);
    }
    return true;
  }
  if (type >= 1 && type <= 3) {
    const int bytes = BytesFor(type);
    if (static_cast<size_t>(width) * height * bytes > imageSize) return false;
    for (int i = 0; i < width * height; ++i) {
      ReadColour(image + static_cast<size_t>(i) * bytes, bytes,
                 &out->rgba[static_cast<size_t>(i) * 4]);
    }
    return true;
  }
  return false;
}

} // namespace ZeroNoTsukaima
