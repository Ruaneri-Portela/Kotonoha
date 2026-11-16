#pragma once
// Decoder for Kirikiri's TLG5 image format; no third-party source bundled.
#include <Kotonoha/core/SDL.hpp>

#include <cstdint>
#include <vector>

namespace Kotonoha::Providers {

// Returns true for the native TLG5/TLG6 and TLG0 structured-data signatures.
bool isTlgImage(const std::vector<std::uint8_t> &bytes);

// Decode TLG5 (3-channel RGB or 4-channel RGBA) to an RGBA32 SDL surface.
// TLG0 wrappers containing TLG5 are accepted. Invalid/truncated images and
// unsupported TLG6 streams throw std::runtime_error.
Kotonoha::SurfacePtr decodeTlgImage(
    const std::vector<std::uint8_t> &bytes);

} // namespace Kotonoha::Providers
