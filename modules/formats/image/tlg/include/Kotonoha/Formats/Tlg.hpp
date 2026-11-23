#pragma once
#include <Kotonoha/Formats/DecodedData.hpp>

#include <cstdint>
#include <vector>

namespace Kotonoha::Formats {

bool isTlgImage(const std::vector<std::uint8_t> &bytes);
DecodedImage decodeTlgImage(const std::vector<std::uint8_t> &bytes);

} // namespace Kotonoha::Formats
