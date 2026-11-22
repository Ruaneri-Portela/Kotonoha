#pragma once

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
  uint8_t slotType = 0; // 1 = eyes, 2 = mouth
  uint8_t groupId = 0;  // character / voice channel
  uint8_t frameCount = 0;
  std::vector<SurfacePtr> frames; // RGBA32 top-down frames
};

struct CanmData {
  std::array<CanmSlot, 4> slots;
};

struct Adp4Decoded {
  std::vector<int16_t> samples;
  std::vector<uint8_t> mouthFrames; // 0xFF or 0x00 every 2205 samples (0.1s)
  int sampleRate = 22050;
  int channels = 1;
};

// Decode 800x600 .CRGB or .BGD image into an RGBA32 top-down SDL_Surface.
SurfacePtr decodeCrgbOrBgd(const uint8_t *data, size_t size);

// Decode scanline alpha-RLE stream (.CHAR or single .PCHR/.ICON frame) into
// an RGBA32 top-down SDL_Surface of dimensions (width x height).
SurfacePtr decodeCharStream(const uint8_t *data, size_t size, int width = 800,
                            int height = 600);

// Decode multi-frame .PCHR (800x600) or .ICON (32x32) container.
std::vector<SurfacePtr> decodePchrOrIcon(const uint8_t *data, size_t size,
                                         int width, int height);

// Decode .PRGB packed raw 24-bit BGR UI sprite bank.
std::vector<SurfacePtr> decodePrgb(const uint8_t *data, size_t size);

// Decode .P256 150x112 CG gallery thumbnails (grayscale when !color, BGR when
// color).
size_t p256ThumbnailCount(size_t size);
SurfacePtr decodeP256Thumbnail(const uint8_t *data, size_t size, size_t index,
                               bool color);

// Decode .CANM character blink / lip-sync overlay animation.
CanmData decodeCanm(const uint8_t *data, size_t size);

// Decode .ALPF (200x150 -> upscaled 800x600) or .ALPM (800x600) wipe mask.
std::vector<uint8_t> decodeAlpfMask(const uint8_t *data, size_t size);
std::vector<uint8_t> decodeAlpmMask(const uint8_t *data, size_t size);
SurfacePtr alphaMaskToSurface(const std::vector<uint8_t> &mask, int width,
                              int height);

// Decode .ADP4 4-bit IMA-ADPCM + silence RLE + rolling XOR voice/SE audio.
Adp4Decoded decodeAdp4(const uint8_t *data, size_t size);

// Convert .SMFD custom stripped MIDI stream to a standard Type-0 .mid buffer.
std::vector<uint8_t> smfdToStandardMidi(const uint8_t *data, size_t size);

// Synthesize .SMFD MIDI stream to stereo 16-bit PCM AudioClip for PcmPlayer.
AudioClip decodeSmfdToPcm(const uint8_t *data, size_t size,
                          int sampleRate = 22050);

} // namespace Kotonoha::Providers
