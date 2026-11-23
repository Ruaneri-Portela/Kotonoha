#include <LucaSystem/CzImage.hpp>
// Shared PAK image codec.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace LucaSystem {
namespace {

std::uint16_t u16(const std::vector<std::uint8_t> &d, size_t p) {
  if (p + 2 > d.size())
    throw std::runtime_error("CZ: truncated uint16");
  return std::uint16_t(d[p]) | (std::uint16_t(d[p + 1]) << 8);
}

std::int16_t s16(const std::vector<std::uint8_t> &d, size_t p) {
  return static_cast<std::int16_t>(u16(d, p));
}

std::uint32_t u32(const std::vector<std::uint8_t> &d, size_t p) {
  if (p + 4 > d.size())
    throw std::runtime_error("CZ: truncated uint32");
  return std::uint32_t(d[p]) | (std::uint32_t(d[p + 1]) << 8) |
         (std::uint32_t(d[p + 2]) << 16) | (std::uint32_t(d[p + 3]) << 24);
}

// CZ2 uses a distinct LSB-first bitstream where each code is preceded by a
// one-bit width flag (0 = 15 bits, 1 = 18 bits). Like the Yoremi fork,
// treat padding as local to each block, never as part of the next block.
std::vector<std::uint8_t> decodeLzw2(const std::vector<std::uint8_t> &data,
                                     size_t offset,
                                     std::uint32_t compressedBytes,
                                     std::uint32_t expected) {
  if (offset > data.size() || compressedBytes > data.size() - offset)
    throw std::runtime_error("CZ2: truncated compressed block");
  const auto bitCount = std::uint64_t(compressedBytes) * 8;
  std::uint64_t bitOffset = 0;

  const auto readBits = [&](int count) {
    std::uint32_t value = 0;
    for (int i = 0; i < count; ++i, ++bitOffset) {
      if (bitOffset < bitCount) {
        const auto byte = data[offset + size_t(bitOffset / 8)];
        value |= std::uint32_t((byte >> (bitOffset % 8)) & 1u) << i;
      }
    }
    return value;
  };

  std::vector<std::vector<std::uint8_t>> dictionary(256);
  for (int i = 0; i < 256; ++i)
    dictionary[i] = {static_cast<std::uint8_t>(i)};
  auto nextCode = std::uint32_t(256);
  auto previous = dictionary[0];
  std::vector<std::uint8_t> out;

  while (out.size() < expected && bitOffset < bitCount) {
    const bool wide = readBits(1) != 0;
    const int width = wide ? 18 : 15;
    // The reference accepts a code ending in the first zero-padding byte,
    // but stops once the byte cursor passes it.
    if ((bitOffset + width) / 8 > compressedBytes)
      break;
    const auto element = readBits(width);
    std::vector<std::uint8_t> entry;
    if (element < dictionary.size() && !dictionary[element].empty()) {
      entry = dictionary[element];
    } else if (element == nextCode) {
      entry = previous;
      entry.push_back(previous.front());
    } else {
      throw std::runtime_error("CZ2: invalid LZW code");
    }
    out.insert(out.end(), entry.begin(), entry.end());
    if (nextCode < 0x40000) {
      auto added = previous;
      added.push_back(entry.front());
      dictionary.push_back(std::move(added));
      ++nextCode;
    }
    previous = std::move(entry);
  }

  if (out.size() < expected)
    throw std::runtime_error("CZ2: short LZW block");
  out.resize(expected);
  return out;
}

std::vector<std::uint8_t> encodeLzw2(const std::vector<std::uint8_t> &pixels) {
  std::vector<std::uint8_t> bytes;
  if (pixels.empty())
    return bytes;
  std::uint32_t bits = 0;
  unsigned count = 0;
  const auto emit = [&](std::uint32_t code) {
    const bool wide = code >= (1u << 15);
    bits |= ((code << 1) | std::uint32_t(wide)) << count;
    count += wide ? 19 : 16;
    while (count >= 8) {
      bytes.push_back(static_cast<std::uint8_t>(bits));
      bits >>= 8;
      count -= 8;
    }
  };
  std::unordered_map<std::uint32_t, std::uint32_t> dictionary;
  // The first decoder insertion (code 256, from the initial zero seed) is
  // reserved. The first real prefix/suffix pair starts at 257.
  std::uint32_t next = 257;
  std::uint32_t prefix = pixels.front();
  for (size_t i = 1; i < pixels.size(); ++i) {
    const auto suffix = pixels[i];
    const auto key = (prefix << 8) | suffix;
    if (const auto it = dictionary.find(key); it != dictionary.end()) {
      prefix = it->second;
      continue;
    }
    emit(prefix);
    if (next < (1u << 18))
      dictionary.emplace(key, next++);
    prefix = suffix;
  }
  emit(prefix);
  if (count != 0)
    bytes.push_back(static_cast<std::uint8_t>(bits));
  return bytes;
}

std::vector<std::uint8_t> decodeLzw(const std::vector<std::uint8_t> &data,
                                    size_t &offset, std::uint32_t codeCount,
                                    std::uint32_t expected) {
  std::vector<std::vector<std::uint8_t>> dictionary(65536);
  for (int i = 0; i < 256; ++i)
    dictionary[i] = {static_cast<std::uint8_t>(i)};
  if (!codeCount)
    return {};
  auto nextCode = std::uint32_t(256);
  const auto first = u16(data, offset);
  if (first >= dictionary.size() || dictionary[first].empty())
    throw std::runtime_error("CZ3: invalid initial LZW code");
  auto previous = dictionary[first];
  std::vector<std::uint8_t> out;
  out.reserve(expected);
  for (std::uint32_t i = 0; i < codeCount; ++i) {
    const auto code = u16(data, offset);
    offset += 2;
    std::vector<std::uint8_t> entry;
    if (code < nextCode && !dictionary[code].empty()) {
      entry = dictionary[code];
    } else if (code == nextCode) {
      entry = previous;
      entry.push_back(previous.front());
    } else {
      throw std::runtime_error("CZ3: invalid LZW code");
    }
    out.insert(out.end(), entry.begin(), entry.end());
    if (nextCode < dictionary.size()) {
      auto added = previous;
      added.push_back(entry.front());
      dictionary[nextCode++] = std::move(added);
    }
    previous = std::move(entry);
  }
  if (out.size() < expected)
    throw std::runtime_error("CZ3: short LZW block");
  out.resize(expected);
  return out;
}

struct AtlasLayout {
  size_t header, table, payload;
  int width, height;
  std::uint32_t blocks;

  explicit AtlasLayout(const std::vector<std::uint8_t> &atlas) {
    if (atlas.size() < 15 || atlas[0] != 'C' || atlas[1] != 'Z' ||
        atlas[2] != '2' || u16(atlas, 12) != 8)
      throw std::runtime_error("Font editing requires an 8-bit CZ2 atlas");
    header = u32(atlas, 4);
    width = u16(atlas, 8);
    height = u16(atlas, 10);
    constexpr size_t paletteSize = 256 * 4;
    if (!width || !height || header < 15 || header > atlas.size() ||
        atlas.size() - header < paletteSize + 4)
      throw std::runtime_error("CZ2: invalid alpha atlas header");
    blocks = u32(atlas, header + paletteSize);
    table = header + paletteSize + 4;
    if (blocks > (atlas.size() - table) / 8)
      throw std::runtime_error("CZ2: truncated alpha atlas block table");
    payload = table + size_t(blocks) * 8;
  }

  template <class Visit>
  size_t visitBlocks(const std::vector<std::uint8_t> &atlas,
                     Visit visit) const {
    size_t offset = payload;
    size_t rawOffset = 0;
    const size_t pixelCount = size_t(width) * height;
    for (size_t i = 0; i < blocks; ++i) {
      const auto row = table + i * 8;
      const auto size = u32(atlas, row);
      const auto raw = u32(atlas, row + 4);
      if (size > atlas.size() - offset || raw > pixelCount - rawOffset)
        throw std::runtime_error("CZ2: alpha block exceeds atlas");
      visit(row, offset, size, rawOffset, raw);
      offset += size;
      rawOffset += raw;
    }
    if (rawOffset != pixelCount)
      throw std::runtime_error("CZ2: incomplete alpha atlas");
    return offset;
  }
};

void putLE(std::vector<std::uint8_t> &bytes, size_t offset, size_t value,
           size_t width) {
  for (size_t i = 0; i < width; ++i)
    bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

} // namespace

std::vector<std::uint8_t>
PatchCz2Alpha(const std::vector<std::uint8_t> &atlas, int x, int y, int width,
              int height, const std::vector<std::uint8_t> &alpha) {
  const AtlasLayout layout(atlas);
  const auto header = layout.header;
  const auto atlasWidth = layout.width;
  const auto atlasHeight = layout.height;
  if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > atlasWidth - width ||
      y > atlasHeight - height ||
      alpha.size() != size_t(width) * size_t(height))
    throw std::runtime_error("CZ2: invalid alpha patch rectangle");
  std::array<std::uint8_t, 256> byAlpha{};
  for (int value = 0; value < 256; ++value) {
    int best = 256;
    for (int i = 0; i < 256; ++i) {
      const int delta = std::abs(int(atlas[header + 4 * i + 3]) - value);
      if (delta < best) {
        best = delta;
        byAlpha[value] = static_cast<std::uint8_t>(i);
      }
    }
  }
  std::vector<std::uint8_t> result(atlas.begin(),
                                   atlas.begin() + layout.payload);
  const auto tail = layout.visitBlocks(
      atlas, [&](size_t row, size_t compressed, uint32_t length,
                 size_t rawOffset, uint32_t raw) {
        const auto end = rawOffset + raw;
        std::vector<std::uint8_t> pixels;
        bool changed = false;
        for (int dy = 0; dy < height; ++dy) {
          const auto start = size_t(y + dy) * atlasWidth + x;
          for (size_t p = std::max(start, rawOffset);
               p < std::min(start + width, end); ++p) {
            if (pixels.empty())
              pixels = decodeLzw2(atlas, compressed, length, raw);
            auto &pixel = pixels[p - rawOffset];
            const auto replacement = alpha[size_t(dy) * width + p - start];
            // Keep the original index when its alpha already matches.
            if (atlas[header + 4 * pixel + 3] != replacement &&
                pixel != byAlpha[replacement]) {
              pixel = byAlpha[replacement];
              changed = true;
            }
          }
        }
        if (changed) {
          const auto block = encodeLzw2(pixels);
          putLE(result, row, block.size(), 4);
          result.insert(result.end(), block.begin(), block.end());
        } else {
          result.insert(result.end(), atlas.begin() + compressed,
                        atlas.begin() + compressed + length);
        }
      });
  result.insert(result.end(), atlas.begin() + tail, atlas.end());
  return result;
}

std::vector<std::uint8_t> GrowCz2Atlas(const std::vector<std::uint8_t> &atlas,
                                       int minWidth, int minHeight) {
  const AtlasLayout layout(atlas);
  const int width = std::max(layout.width, minWidth);
  const int height = std::max(layout.height, minHeight);
  if (width > UINT16_MAX || height > UINT16_MAX)
    throw std::runtime_error("CZ2: grown atlas exceeds dimension limit");
  if (width == layout.width && height == layout.height)
    return atlas;

  int transparent = 0;
  while (transparent < 256 && atlas[layout.header + 4 * transparent + 3] != 0)
    ++transparent;
  if (transparent == 256)
    throw std::runtime_error(
        "CZ2: cannot grow atlas without transparent entry");
  const bool reflow = width != layout.width;
  const int firstRow = reflow ? 0 : layout.height;
  const int rowsPerBlock = std::max(1, (128 * 1024) / width);
  const size_t preserved = reflow ? 0 : layout.blocks;
  const size_t added = (height - firstRow + rowsPerBlock - 1) / rowsPerBlock;
  if (added > UINT32_MAX - preserved ||
      preserved + added > (atlas.max_size() - layout.table) / 8)
    throw std::runtime_error("CZ2: grown atlas block table exceeds size limit");
  std::vector<std::uint8_t> result(atlas.begin(), atlas.begin() + layout.table);
  result.resize(layout.table + (preserved + added) * 8);
  putLE(result, 8, width, 2);
  putLE(result, 10, height, 2);
  putLE(result, layout.table - 4, preserved + added, 4);
  std::vector<std::uint8_t> oldPixels;
  const auto tail =
      layout.visitBlocks(atlas, [&](size_t row, size_t offset, uint32_t length,
                                    size_t, uint32_t raw) {
        if (reflow) {
          const auto pixels = decodeLzw2(atlas, offset, length, raw);
          oldPixels.insert(oldPixels.end(), pixels.begin(), pixels.end());
        } else {
          std::copy_n(atlas.begin() + row, 8, result.begin() + row);
          result.insert(result.end(), atlas.begin() + offset,
                        atlas.begin() + offset + length);
        }
      });
  size_t row = layout.table + preserved * 8;
  for (int y = firstRow; y < height; y += rowsPerBlock) {
    const int rows = std::min(rowsPerBlock, height - y);
    std::vector<std::uint8_t> pixels(size_t(rows) * width,
                                     static_cast<std::uint8_t>(transparent));
    if (reflow) {
      for (int i = 0; i < rows && y + i < layout.height; ++i)
        std::copy_n(oldPixels.begin() + size_t(y + i) * layout.width,
                    layout.width, pixels.begin() + size_t(i) * width);
    }
    const auto block = encodeLzw2(pixels);
    putLE(result, row, block.size(), 4);
    putLE(result, row + 4, pixels.size(), 4);
    row += 8;
    result.insert(result.end(), block.begin(), block.end());
  }
  result.insert(result.end(), atlas.begin() + tail, atlas.end());
  return result;
}

CzImage DecodeCzImage(const std::vector<std::uint8_t> &data) {
  if (data.size() < 15 || data[0] != 'C' || data[1] != 'Z')
    throw std::runtime_error("Not a CZ image");
  const auto kind = data[2];
  const auto headerLength = u32(data, 4);
  if (headerLength < 15 || headerLength > data.size())
    throw std::runtime_error("CZ: invalid header length");

  CzImage image;
  image.width = u16(data, 8);
  image.height = u16(data, 10);
  image.cropWidth = image.width;
  image.cropHeight = image.height;
  image.canvasWidth = image.width;
  image.canvasHeight = image.height;
  if (headerLength >= 28) {
    image.cropX = s16(data, 16);
    image.cropY = s16(data, 18);
    image.cropWidth = u16(data, 20);
    image.cropHeight = u16(data, 22);
    image.canvasWidth = u16(data, 24);
    image.canvasHeight = u16(data, 26);
  }
  if (headerLength >= 36) {
    image.anchorX = s16(data, 28);
    image.anchorY = s16(data, 30);
  }

  auto bits = u16(data, 12);
  if ((kind == '1' || kind == '2') && bits > 32)
    bits = 8;
  if (image.width <= 0 || image.height <= 0 ||
      (bits != 4 && bits != 8 && bits != 24 && bits != 32))
    throw std::runtime_error("CZ: unsupported dimensions or color depth");
  const auto pixelCount = size_t(image.width) * size_t(image.height);
  if (pixelCount > image.rgba.max_size() / 4)
    throw std::runtime_error("CZ: image size out of range");
  const auto pixelBytes = pixelCount * 4;

  if (kind == '0') {
    if (headerLength + pixelBytes > data.size())
      throw std::runtime_error("CZ0: truncated pixels");
    image.rgba.assign(data.begin() + headerLength,
                      data.begin() + headerLength + pixelBytes);
    return image;
  }
  if (kind != '1' && kind != '2' && kind != '3' && kind != '4')
    throw std::runtime_error("CZ: decoder supports CZ0/CZ1/CZ2/CZ3/CZ4 only");
  size_t blockTable = headerLength;
  std::vector<std::array<std::uint8_t, 4>> palette;
  if ((kind == '1' || kind == '2') && (bits == 4 || bits == 8)) {
    const size_t colors = size_t(1) << bits;
    if (blockTable + colors * 4 > data.size())
      throw std::runtime_error("CZ: truncated palette");
    palette.resize(colors);
    for (size_t i = 0; i < colors; ++i) {
      const auto p = blockTable + i * 4;
      if (kind == '2') {
        // CZ2 font/mask palettes are stored as BGRA, matching LuckSystem's
        // czimage ColorPanel (R = byte 2, G = byte 1, B = byte 0).
        palette[i] = {data[p + 2], data[p + 1], data[p], data[p + 3]};
      } else {
        // CZ1 palettes store RGBA byte order directly (verified against
        // EVENTCG.PAK 32bpp ground truth).
        palette[i] = {data[p], data[p + 1], data[p + 2], data[p + 3]};
      }
    }
    blockTable += colors * 4;
  }
  if (blockTable + 4 > data.size())
    throw std::runtime_error("CZ: truncated block table");

  const auto blockCount = u32(data, blockTable);
  const size_t table = blockTable + 4;
  if (blockCount > (data.size() - table) / 8)
    throw std::runtime_error("CZ: invalid block table");
  size_t compressed = table + size_t(blockCount) * 8;
  // LuckSystem's czimage/imagefix.go LineDiff reads exactly
  // Width*Height*(Colorbits/8) bytes for CZ3. Its 24-bit form has RGB only:
  // treating it as 32-bit makes valid CZ3 assets fail as "incomplete image".
  if ((kind == '3' && bits != 24 && bits != 32) || (kind == '4' && bits != 32))
    throw std::runtime_error("CZ3/CZ4: unsupported color depth");
  const size_t rawBytes =
      kind == '1' || kind == '2'
          ? (bits == 4 ? (pixelCount + 1) / 2 : pixelCount * (bits / 8))
          : pixelCount * (kind == '3' ? (bits / 8) : 4);
  std::vector<std::uint8_t> pixels;
  if (kind != '2')
    pixels.reserve(pixelBytes);
  for (std::uint32_t i = 0; i < blockCount; ++i) {
    const auto codes = u32(data, table + size_t(i) * 8);
    const auto raw = u32(data, table + size_t(i) * 8 + 4);
    std::vector<std::uint8_t> block;
    if (kind == '2') {
      if (raw > rawBytes - pixels.size())
        throw std::runtime_error("CZ2: block exceeds image size");
      block = decodeLzw2(data, compressed, codes, raw);
      compressed += codes;
    } else {
      block = decodeLzw(data, compressed, codes, raw);
    }
    pixels.insert(pixels.end(), block.begin(), block.end());
  }
  if (pixels.size() < rawBytes)
    throw std::runtime_error("CZ: incomplete image");
  pixels.resize(rawBytes);

  if (kind == '2') {
    if (palette.empty())
      throw std::runtime_error("CZ2: unsupported color depth");
    std::vector<std::uint8_t> rgba(pixelBytes);
    for (size_t i = 0; i < pixelCount; ++i) {
      const size_t index =
          bits == 4 ? ((i & 1) ? (pixels[i / 2] >> 4) : (pixels[i / 2] & 0x0f))
                    : pixels[i];
      std::copy(palette[index].begin(), palette[index].end(),
                rgba.begin() + i * 4);
    }
    image.rgba = std::move(rgba);
    return image;
  }

  if (kind == '1') {
    std::vector<std::uint8_t> rgba(pixelBytes);
    const size_t count = size_t(image.width) * size_t(image.height);
    if (bits == 8 && pixels.size() >= count) {
      for (size_t i = 0; i < count; ++i)
        std::copy(palette.at(pixels[i]).begin(), palette.at(pixels[i]).end(),
                  rgba.begin() + i * 4);
    } else if (bits == 4 && pixels.size() * 2 >= count) {
      for (size_t i = 0; i < count; ++i) {
        const auto index =
            (i & 1) ? (pixels[i / 2] >> 4) : (pixels[i / 2] & 0x0f);
        std::copy(palette.at(index).begin(), palette.at(index).end(),
                  rgba.begin() + i * 4);
      }
    } else if (bits == 24 && pixels.size() >= count * 3) {
      for (size_t i = 0; i < count; ++i) {
        rgba[i * 4] = pixels[i * 3];
        rgba[i * 4 + 1] = pixels[i * 3 + 1];
        rgba[i * 4 + 2] = pixels[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
      }
    } else if (bits == 32) {
      rgba = std::move(pixels);
    } else {
      throw std::runtime_error("CZ1: incomplete pixels");
    }
    image.rgba = std::move(rgba);
    return image;
  }

  const size_t stride = size_t(image.width) * (bits / 8);
  const int blockHeight = (image.height + 2) / 3;
  if (kind == '4') {
    const size_t rgbSize = size_t(image.width) * size_t(image.height) * 3;
    std::vector<std::uint8_t> rgba(pixelBytes);
    std::vector<std::uint8_t> previousRgb(size_t(image.width) * 3);
    std::vector<std::uint8_t> previousAlpha(size_t(image.width));
    for (int y = 0; y < image.height; ++y) {
      const size_t rgbLine = size_t(y) * size_t(image.width) * 3;
      const size_t alphaLine = rgbSize + size_t(y) * size_t(image.width);
      for (int x = 0; x < image.width; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
          auto value = pixels[rgbLine + size_t(x) * 3 + channel];
          if (y % blockHeight != 0)
            value = std::uint8_t(value + previousRgb[size_t(x) * 3 + channel]);
          previousRgb[size_t(x) * 3 + channel] = value;
          rgba[(size_t(y) * image.width + x) * 4 + channel] = value;
        }
        auto alpha = pixels[alphaLine + x];
        if (y % blockHeight != 0)
          alpha = std::uint8_t(alpha + previousAlpha[x]);
        previousAlpha[x] = alpha;
        rgba[(size_t(y) * image.width + x) * 4 + 3] = alpha;
      }
    }
    image.rgba = std::move(rgba);
    return image;
  }
  for (int y = 0; y < image.height; ++y) {
    if (y % blockHeight == 0)
      continue;
    const size_t line = size_t(y) * stride;
    const size_t previous = line - stride;
    for (size_t x = 0; x < stride; ++x)
      pixels[line + x] = std::uint8_t(pixels[line + x] + pixels[previous + x]);
  }
  if (kind == '3' && bits == 24) {
    // The reference's LineDiff emits NRGBA{RGB,255} from delta-decoded
    // 24-bit rows; never read a phantom fourth byte as alpha.
    image.rgba.resize(pixelBytes);
    for (size_t i = 0; i < pixelCount; ++i) {
      image.rgba[i * 4] = pixels[i * 3];
      image.rgba[i * 4 + 1] = pixels[i * 3 + 1];
      image.rgba[i * 4 + 2] = pixels[i * 3 + 2];
      image.rgba[i * 4 + 3] = 255;
    }
  } else {
    image.rgba = std::move(pixels);
  }
  return image;
}

} // namespace LucaSystem
