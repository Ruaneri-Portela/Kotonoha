#pragma once

#include <Kotonoha/core/AssetProvider.hpp>
#include <Kotonoha/core/SDL.hpp>

#include <vector>

namespace Kotonoha {

// Whole-asset FFmpeg decoding of in-memory media (anything libavformat
// probes: PNG/JPEG/WebP/TIFF... pictures, video files, MP3/OGG/FLAC/WAV...).
// Streaming playback is MediaPlayer's job; these return complete objects.
namespace MediaDecoder {

// A picture, or the video frame shown at `seconds`, as an RGBA32 surface.
SurfacePtr decodePicture(const std::vector<Uint8> &bytes, double seconds = 0);
// The first audio stream as interleaved F32 PCM at its own rate (layouts with
// more than 8 channels are downmixed to stereo).
AudioClip decodeAudio(const std::vector<Uint8> &bytes);
// Container duration in seconds, or a negative value when unknown.
double duration(const std::vector<Uint8> &bytes);

} // namespace MediaDecoder
} // namespace Kotonoha
