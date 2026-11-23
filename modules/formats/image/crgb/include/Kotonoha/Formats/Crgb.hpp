#pragma once
#include <Kotonoha/Formats/DecodedData.hpp>

#include <cstddef>
#include <vector>

namespace Kotonoha::Formats::Crgb {
DecodedImage decodeCrgbOrBgd(const uint8_t *data, size_t size);
DecodedImage decodeCharStream(const uint8_t *data, size_t size, int width = 800,
                              int height = 600);
std::vector<DecodedImage> decodePchrOrIcon(const uint8_t *data, size_t size,
                                           int width, int height);
std::vector<DecodedImage> decodePrgb(const uint8_t *data, size_t size);
size_t p256ThumbnailCount(size_t size);
DecodedImage decodeP256Thumbnail(const uint8_t *data, size_t size, size_t index,
                                 bool color);
CanmData decodeCanm(const uint8_t *data, size_t size);
std::vector<uint8_t> decodeAlpfMask(const uint8_t *data, size_t size);
std::vector<uint8_t> decodeAlpmMask(const uint8_t *data, size_t size);
DecodedImage alphaMaskToImage(const std::vector<uint8_t> &mask, int width,
                              int height);
} // namespace Kotonoha::Formats::Crgb
