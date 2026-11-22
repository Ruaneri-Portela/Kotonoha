// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace Ponscripter {

// Apply scene-only legacy color effects to an RGBA32 buffer. Input is in
// R,G,B,A byte order; alpha is preserved. Negative mode 1 precedes the
// monochrome tint, while negative mode 2 follows it.
inline void applyScenePixelEffects(std::uint8_t *pixels, int width, int height,
                                   int pitch, bool monochrome,
                                   std::uint32_t tint, int negativeMode) {
  if (!pixels || width <= 0 || height <= 0 || pitch <= 0 ||
      static_cast<std::size_t>(width) * 4 >
          static_cast<std::size_t>(pitch))
    return;

  const int tintR = static_cast<int>((tint >> 16) & 0xFFu);
  const int tintG = static_cast<int>((tint >> 8) & 0xFFu);
  const int tintB = static_cast<int>(tint & 0xFFu);
  for (int y = 0; y < height; ++y) {
    auto *row = pixels + static_cast<std::size_t>(y) * pitch;
    for (int x = 0; x < width; ++x) {
      std::uint8_t *pixel = row + static_cast<std::size_t>(x) * 4;
      int r = pixel[0], g = pixel[1], b = pixel[2];
      if (negativeMode == 1) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
      }
      if (monochrome) {
        const int luma = (77 * r + 150 * g + 29 * b + 128) >> 8;
        r = (luma * tintR + 127) / 255;
        g = (luma * tintG + 127) / 255;
        b = (luma * tintB + 127) / 255;
      }
      if (negativeMode == 2) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
      }
      pixel[0] = static_cast<std::uint8_t>(r);
      pixel[1] = static_cast<std::uint8_t>(g);
      pixel[2] = static_cast<std::uint8_t>(b);
    }
  }
}

} // namespace Ponscripter
