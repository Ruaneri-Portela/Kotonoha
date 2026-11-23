#pragma once
// Low-level Summer Radish / MEO byte codecs shared by the archive provider and
// the image decoders. Re-exported by SummerRadish/Pack.hpp.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SummerRadish {

using Bytes = std::vector<uint8_t>;

uint32_t word(const Bytes &, size_t);
Bytes decompress(const Bytes &, size_t size, size_t offset = 0);
Bytes decodeStackWork(const Bytes &);
std::string utf16(const Bytes &, size_t offset, size_t units);
std::string canonical(std::string);

} // namespace SummerRadish
