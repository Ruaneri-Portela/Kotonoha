#pragma once
#include <Kotonoha/Formats/DecodedData.hpp>

#include <cstddef>

namespace Kotonoha::Formats::Adpcm {
Adp4Decoded decodeAdp4(const uint8_t *data, size_t size);
} // namespace Kotonoha::Formats::Adpcm
