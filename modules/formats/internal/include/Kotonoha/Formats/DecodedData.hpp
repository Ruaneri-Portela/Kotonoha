#pragma once
// Neutral decoded data returned by every modules/formats codec.
// This header depends only on the C++ standard library: no SDL, engine or
// presentation types are permitted in modules/formats.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kotonoha::Formats {

// Top-down RGBA8888 pixels plus the anchor on the destination canvas.
struct DecodedImage {
  uint32_t width = 0;
  uint32_t height = 0;
  int32_t x = 0;
  int32_t y = 0;
  std::vector<uint8_t> rgba;
};

// Interleaved signed 16-bit PCM plus its format description.
struct DecodedPcm {
  std::vector<int16_t> samples;
  int sampleRate = 0;
  int channels = 0;
};

// One animation frame: the neutral RGBA blob and its explicit frame index.
struct DecodedFrame {
  size_t index = 0;
  DecodedImage image;
};

// Block-coded PCM (MEO SND/BGM): loop offset in interleaved frames.
struct BgmPcm {
  DecodedPcm pcm;
  std::size_t loopStart = 0;
};

// CANM blink/lip-sync slot using neutral frames.
struct CanmSlot {
  uint16_t x = 0, y = 0, width = 0, height = 0;
  uint8_t slotType = 0, groupId = 0, frameCount = 0;
  std::vector<DecodedImage> frames;
};
struct CanmData {
  std::array<CanmSlot, 4> slots;
};

// ADP4 decoded voice plus the mouth-sync flags derived from its samples.
struct Adp4Decoded {
  std::vector<int16_t> samples;
  std::vector<uint8_t> mouthFrames;
  int sampleRate = 22050;
  int channels = 1;
};

} // namespace Kotonoha::Formats
