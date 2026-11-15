#pragma once

#include <cstddef>
#include <cstdint>

namespace Kotonoha {

// Prepares straight-alpha RGBA pixels for bilinear sampling by copying RGB
// into only the fully transparent texels immediately adjacent to visible ones.
// Alpha and every visible texel remain byte-exact.
std::size_t prepareStraightAlphaForLinearFiltering(
    std::uint8_t *rgba, int width, int height, int pitch);

} // namespace Kotonoha
