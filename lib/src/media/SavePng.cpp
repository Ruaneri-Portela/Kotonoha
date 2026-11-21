#include <Kotonoha/media/SavePng.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_version.h>

#if !SDL_VERSION_ATLEAST(3, 4, 0)
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#endif

namespace Kotonoha {

bool SavePng(SDL_Surface *surface, const char *filename) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
  return SDL_SavePNG(surface, filename);
#else
  if (!surface || !filename || surface->w <= 0 || surface->h <= 0)
    return SDL_SetError("Invalid surface or filename for PNG export");

  // Normalize channel order and pitch; PNG requires RGBA bytes in that order.
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
      SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!rgba)
    return false;

  const size_t rowBytes = static_cast<size_t>(rgba->w) * 4;
  const size_t height = static_cast<size_t>(rgba->h);
  if (rgba->w > std::numeric_limits<int>::max() / 4 ||
      rgba->pitch < 0 || static_cast<size_t>(rgba->pitch) < rowBytes ||
      height > (std::numeric_limits<size_t>::max() / (rowBytes + 1)))
    return SDL_SetError("PNG dimensions or stride are invalid");

  const bool locked = SDL_MUSTLOCK(rgba.get());
  if (locked && !SDL_LockSurface(rgba.get()))
    return false;

  // A valid PNG can contain uncompressed DEFLATE blocks. Debug exports are
  // intentionally lossless and require no libpng/SDL3_image/zlib dependency.
  std::vector<std::uint8_t> scanlines;
  scanlines.reserve(height * (rowBytes + 1));
  const auto *pixels = static_cast<const std::uint8_t *>(rgba->pixels);
  for (size_t y = 0; y < height; ++y) {
    scanlines.push_back(0);  // PNG filter: None.
    const auto *row = pixels + y * static_cast<size_t>(rgba->pitch);
    scanlines.insert(scanlines.end(), row, row + rowBytes);
  }
  if (locked)
    SDL_UnlockSurface(rgba.get());

  auto put32 = [](std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
  };
  auto checksum = [](const std::uint8_t *bytes, size_t length) {
    std::uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
      crc ^= bytes[i];
      for (int b = 0; b < 8; ++b)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
  };

  // ZLIB stream with DEFLATE 'stored' blocks (RFC 1950 / RFC 1951).
  std::vector<std::uint8_t> compressed{0x78, 0x01};
  std::uint32_t adlerA = 1, adlerB = 0;
  for (auto byte : scanlines) {
    adlerA = (adlerA + byte) % 65521;
    adlerB = (adlerB + adlerA) % 65521;
  }
  for (size_t offset = 0; offset < scanlines.size();) {
    const auto length = static_cast<std::uint16_t>(
        std::min<size_t>(65535, scanlines.size() - offset));
    const bool final = offset + length == scanlines.size();
    compressed.push_back(final ? 1 : 0);
    compressed.push_back(static_cast<std::uint8_t>(length));
    compressed.push_back(static_cast<std::uint8_t>(length >> 8));
    compressed.push_back(static_cast<std::uint8_t>(~length));
    compressed.push_back(static_cast<std::uint8_t>((~length) >> 8));
    compressed.insert(compressed.end(), scanlines.begin() + offset,
                      scanlines.begin() + offset + length);
    offset += length;
  }
  put32(compressed, (adlerB << 16) | adlerA);

  if (compressed.size() > std::numeric_limits<std::uint32_t>::max())
    return SDL_SetError("PNG export exceeds the maximum IDAT chunk size");

  std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
  auto chunk = [&](const char *type, const std::vector<std::uint8_t> &data) {
    put32(png, static_cast<std::uint32_t>(data.size()));
    const size_t begin = png.size();
    for (int i = 0; i < 4; ++i)
      png.push_back(static_cast<std::uint8_t>(type[i]));
    png.insert(png.end(), data.begin(), data.end());
    put32(png, checksum(png.data() + begin, 4 + data.size()));
  };

  std::vector<std::uint8_t> header;
  put32(header, static_cast<std::uint32_t>(rgba->w));
  put32(header, static_cast<std::uint32_t>(rgba->h));
  header.insert(header.end(), {8, 6, 0, 0, 0}); // RGBA8, no interlace.
  chunk("IHDR", header);
  chunk("IDAT", compressed);
  chunk("IEND", {});

  SDL_IOStream *output = SDL_IOFromFile(filename, "wb");
  if (!output)
    return false;
  const size_t written = SDL_WriteIO(output, png.data(), png.size());
  const bool closed = SDL_CloseIO(output);
  if (written != png.size())
    return SDL_SetError("Cannot write complete PNG file: %s", filename);
  return closed;
#endif
}

}  // namespace Kotonoha
