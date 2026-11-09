#pragma once
#include <Kotonoha/core/SDL.hpp>
#include <cstdint>
#include <map>

namespace RealLive {
struct MouseCursorFrame {
  Kotonoha::SurfacePtr surface;
  int hotspotX = 0;
  int hotspotY = 0;
};

// MCUR/KCUR atlases are 400x88: an 8px border, twelve 32x32 colour frames and,
// below them, a hotspot map with one white pixel per frame. The chroma colour
// differs per game (red in Clannad, green in Kanon/AIR, blue in Planetarian),
// so use the dominant colour along the frame edge as the transparent key.
inline MouseCursorFrame mouseCursorFrame(SDL_Surface *atlas) {
  constexpr int border = 8;
  constexpr int size = 32;
  if (!atlas || atlas->w != 400 || atlas->h != 88)
    return {};

  std::map<std::uint32_t, int> edgeColours;
  auto sample = [&](int x, int y) {
    Uint8 r, g, b, a;
    if (!SDL_ReadSurfacePixel(atlas, x, y, &r, &g, &b, &a))
      return false;
    ++edgeColours[(std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b];
    return true;
  };
  for (int i = 0; i < size; ++i)
    if (!sample(border + i, border) || !sample(border + i, border + size - 1) ||
        !sample(border, border + i) || !sample(border + size - 1, border + i))
      return {};

  std::uint32_t key = 0;
  int best = 0;
  for (const auto &[colour, count] : edgeColours)
    if (count > best) {
      key = colour;
      best = count;
    }
  if (!best)
    return {};

  MouseCursorFrame result;
  result.surface.reset(SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32));
  if (!result.surface)
    return {};
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      Uint8 r, g, b, a;
      if (!SDL_ReadSurfacePixel(atlas, x + border, y + border, &r, &g, &b, &a))
        return {};
      const auto colour =
          (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
      if (!SDL_WriteSurfacePixel(result.surface.get(), x, y, r, g, b,
                                 colour == key ? 0 : 255))
        return {};
    }

  // The lower strip stores the hotspot of each frame as a single white pixel.
  for (int y = 0; y < size && result.hotspotX == 0 && result.hotspotY == 0; ++y)
    for (int x = 0; x < size; ++x) {
      Uint8 r, g, b, a;
      if (SDL_ReadSurfacePixel(atlas, x + border, y + 48, &r, &g, &b, &a) &&
          r > 240 && g > 240 && b > 240) {
        result.hotspotX = x;
        result.hotspotY = y;
        break;
      }
    }
  return result;
}
} // namespace RealLive
