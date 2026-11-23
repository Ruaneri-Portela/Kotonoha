#pragma once
// Decoded-data types used by the Pure Mail provider's public API. The codecs
// themselves live in modules/formats and return neutral Kotonoha::Formats data.
#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kotonoha::Providers {

struct CanmSlot {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t slotType = 0;
  uint8_t groupId = 0;
  uint8_t frameCount = 0;
  std::vector<SurfacePtr> frames;
};

struct CanmData {
  std::array<CanmSlot, 4> slots;
};

struct Adp4Decoded {
  std::vector<int16_t> samples;
  std::vector<uint8_t> mouthFrames;
  int sampleRate = 22050;
  int channels = 1;
};

} // namespace Kotonoha::Providers
