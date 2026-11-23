#pragma once
#include <Kotonoha/Formats/DecodedData.hpp>

#include <cstddef>
#include <vector>

namespace Kotonoha::Formats::BlockPcm {

BgmPcm decodeBgm(const std::vector<uint8_t> &bytes);
DecodedPcm decodeSnd(const std::vector<uint8_t> &bytes);

} // namespace Kotonoha::Formats::BlockPcm
