#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ZeroNoTsukaima {

// Decodes headerless PS2 SPU-ADPCM (16-byte blocks: shift/predictor, flags, 28 samples)
// into mono 16-bit PCM. Stops after a block flagged as the end (1 or 7).
std::vector<int16_t> DecodeVag(const uint8_t *data, size_t size);

} // namespace ZeroNoTsukaima
