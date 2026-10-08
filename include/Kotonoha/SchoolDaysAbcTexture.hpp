#pragma once

#include <cstddef>
#include <cstdint>

namespace Kotonoha {

// Prepare a straight-alpha RGBA staging buffer for bilinear sampling.
// Only RGB of fully transparent texels immediately adjacent to visible
// texels may change. Alpha and all visible RGBA texels remain untouched.
std::size_t PrepareStraightAlphaForLinearFiltering(
    std::uint8_t* rgba, int width, int height, int pitch);

} // namespace Kotonoha
