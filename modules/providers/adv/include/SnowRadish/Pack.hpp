#pragma once
// Overflow DAT archive directory reader (container layer).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SnowRadish {

// LZSS used by the Overflow ADV3 engine (LOCPU.DLL DecodeLz): 4 KiB zeroed
// window, write cursor starts at 0xFEE, MSB-first flags, match = u16 LE with
// length (d & 15) + 3 and absolute window position d >> 4.
// Returns the number of source bytes consumed.
size_t lzssDecode(const uint8_t *src, size_t srcSize, uint8_t *dst,
                  size_t dstSize);

struct PackEntry {
  uint32_t offset = 0;
  uint32_t compressedSize = 0;
  uint32_t rawSize = 0;
  uint32_t id = 0;
  std::string name;
};

// Reads the compressed directory of a *.DAT pack. The last 12 bytes are
// three u32 values XORed with 0xF0F0F0F0: directory compressed size, directory
// raw size and a checksum. Each 80-byte record is {id, name[64], offset,
// compSize, rawSize}; compSize == rawSize means the entry is stored.
std::vector<PackEntry> readDirectory(const std::vector<uint8_t> &pack);

// Entries with an unknown raw size are decoded until their compressed bytes run
// out.
std::vector<uint8_t> extractEntry(const std::vector<uint8_t> &pack,
                                  const PackEntry &entry);

} // namespace SnowRadish
