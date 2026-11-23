// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
// One-time CPU preparation for SDL renderer GPU textures. The original
// NitroSharp shaders apply monochrome and box blur on GPU; this portable
// fallback produces equivalent categories of effects without per-frame CPU
// rasterization. Future renderers can replace it with dedicated shaders.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace N2System {

inline void grayscaleRgba(std::vector<uint8_t> &pixels) {
  for (std::size_t i=0;i+3<pixels.size();i+=4) {
    const unsigned gray = (77u*pixels[i] + 150u*pixels[i+1] +
                           29u*pixels[i+2] + 128u) >> 8;
    pixels[i] = pixels[i+1] = pixels[i+2] = uint8_t(gray);
    // Alpha stays unchanged, including anti-aliased sprite silhouettes.
  }
}

// 3x3 premultiplied box filter; repeated passes approximate NitroSharp's
// medium/heavy shader presets without causing black fringes around alpha.
inline bool boxBlurRgba(std::vector<uint8_t> &pixels, int width, int height,
                        unsigned passes) {
  if (width <= 0 || height <= 0 || passes > 16 ||
      std::size_t(width) > std::numeric_limits<std::size_t>::max() /
                                std::size_t(height) / 4 ||
      pixels.size() != std::size_t(width) * std::size_t(height) * 4)
    return false;
  std::vector<uint8_t> next(pixels.size());
  for (unsigned pass=0;pass<passes;++pass) {
    for(int y=0;y<height;++y) {
      for(int x=0;x<width;++x) {
        unsigned sumA=0,sumR=0,sumG=0,sumB=0;
        for(int oy=-1;oy<=1;++oy) {
          const int sy=std::clamp(y+oy,0,height-1);
          for(int ox=-1;ox<=1;++ox) {
            const int sx=std::clamp(x+ox,0,width-1);
            const std::size_t p=(std::size_t(sy)*width+sx)*4;
            const unsigned a=pixels[p+3];
            sumA+=a;
            sumR+=pixels[p]*a;
            sumG+=pixels[p+1]*a;
            sumB+=pixels[p+2]*a;
          }
        }
        const std::size_t i=(std::size_t(y)*width+x)*4;
        next[i]=sumA?uint8_t(sumR/sumA):0;
        next[i+1]=sumA?uint8_t(sumG/sumA):0;
        next[i+2]=sumA?uint8_t(sumB/sumA):0;
        next[i+3]=uint8_t((sumA+4u)/9u);
      }
    }
    pixels.swap(next);
  }
  return true;
}

} // namespace N2System
