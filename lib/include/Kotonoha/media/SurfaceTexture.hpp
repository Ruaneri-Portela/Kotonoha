#pragma once

#include <Kotonoha/core/SDL.hpp>

namespace Kotonoha {

// Return a new RGBA32 surface suitable for straight-alpha bilinear sampling.
// The source is never modified, including its blend mode, color key, palette,
// and pixel bytes. Pixel-format conversion and SDL color-key semantics are
// resolved before filling the transparent one-pixel RGB border.
SurfacePtr prepareSurfaceForLinearSampling(SDL_Surface *source);

} // namespace Kotonoha
