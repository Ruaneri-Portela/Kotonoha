#pragma once
// Decoder for Kirikiri's TLG5 image format; no third-party source bundled.
#include <Kotonoha/core/SDL.hpp>

#include <cstdint>
#include <vector>

namespace Kotonoha::Providers {

// Returns true for the native TLG5/TLG6 and TLG0 structured-data signatures.
bool isTlgImage(const std::vector<std::uint8_t> &bytes);

// Decode TLG5 RGB/RGBA and TLG6 Golomb-coded 1/3/4-channel images to an
// RGBA32 SDL surface. TLG0 structured wrappers are accepted.
// Non-Golomb TLG6 entropy methods and malformed streams throw.
Kotonoha::SurfacePtr decodeTlgImage(
    const std::vector<std::uint8_t> &bytes);

} // namespace Kotonoha::Providers
