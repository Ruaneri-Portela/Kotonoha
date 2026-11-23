// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Kotonoha/core/SDL.hpp>
#include <Kotonoha/Formats/DecodedData.hpp>

#include <SDL3/SDL.h>

#include <cstring>

namespace Kotonoha::media {

// Convert a neutral modules/formats image into an SDL surface. The codec side
// never constructs SDL objects; the presentation side does.
inline SurfacePtr surfaceFromDecoded(const Formats::DecodedImage &img) {
  if (img.width == 0 || img.height == 0)
    return nullptr;
  SurfacePtr surface(SDL_CreateSurface(static_cast<int>(img.width),
                                       static_cast<int>(img.height),
                                       SDL_PIXELFORMAT_RGBA32));
  if (!surface)
    return nullptr;
  if (!img.rgba.empty())
    std::memcpy(surface->pixels, img.rgba.data(), img.rgba.size());
  return surface;
}

} // namespace Kotonoha::media
