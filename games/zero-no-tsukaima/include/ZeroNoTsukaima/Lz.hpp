#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ZeroNoTsukaima {

// Container of every compressed asset: u32 uncompressed size, u32 mode, then
// a command stream (recovered from the PS2 executable routine at 0x10f730):
//   111nnnnn  literal run of n+1 bytes
//   110nnnnn  byte repeated n+2 times
//   iiinnnnn  (i<6) replay the i-th most recent command with a new length;
//             the replayed command moves to the front of a 6-entry MRU list
// The MRU list starts filled with a "repeat 0x00" command. `mode` is a byte
// stride: after the command stream is expanded the bytes are de-interleaved
// (they were stored as `mode` planes).
struct PackedHeader {
  uint32_t size = 0;
  uint32_t mode = 0;
};

bool ReadPackedHeader(const uint8_t *data, size_t size, PackedHeader *out);

// Returns false on a truncated stream or if the output size does not match.
bool Unpack(const uint8_t *data, size_t size, std::vector<uint8_t> *out);

} // namespace ZeroNoTsukaima
