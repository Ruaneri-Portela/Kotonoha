#pragma once
#include <Kotonoha/Formats/DecodedData.hpp>

#include <cstddef>
#include <vector>

namespace Kotonoha::Formats::Smfd {
std::vector<uint8_t> smfdToStandardMidi(const uint8_t *data, size_t size);
DecodedPcm decodeSmfdToPcm(const uint8_t *data, size_t size,
                           int sampleRate = 22050);
} // namespace Kotonoha::Formats::Smfd
