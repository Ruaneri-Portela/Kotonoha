#pragma once
#include <cstdint>
#include <vector>

namespace ZeroNoTsukaima {

// NAME.HD is an array of u32 entry sizes; NAME.BIN stores entry i at the next
// 2048-byte boundary after entry i-1. The disc itself is read through
// Kotonoha::DiscImage.
struct ArchiveEntry {
  uint64_t offset = 0;
  uint32_t size = 0;
};

std::vector<ArchiveEntry> ParseArchiveIndex(const std::vector<uint8_t> &hd);

} // namespace ZeroNoTsukaima
