#include <LucaSystem/CzImage.hpp>

#include <array>
#include <stdexcept>
#include <string>

namespace LucaSystem {
namespace {

std::uint16_t u16(const std::vector<std::uint8_t> &d, size_t p) {
  if (p + 2 > d.size())
    throw std::runtime_error("CZ: truncated uint16");
  return std::uint16_t(d[p]) | (std::uint16_t(d[p + 1]) << 8);
}

std::uint32_t u32(const std::vector<std::uint8_t> &d, size_t p) {
  if (p + 4 > d.size())
    throw std::runtime_error("CZ: truncated uint32");
  return std::uint32_t(d[p]) | (std::uint32_t(d[p + 1]) << 8) |
         (std::uint32_t(d[p + 2]) << 16) | (std::uint32_t(d[p + 3]) << 24);
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

} // namespace

CzImage DecodeCzImage(const std::vector<std::uint8_t> &data) {
  if (data.size() < 15 || data[0] != 'C' || data[1] != 'Z')
    throw std::runtime_error("Not a CZ image");
  const auto kind = data[2];
  const auto headerLength = u32(data, 4);
  CzImage image;
  image.width = u16(data, 8);
  image.height = u16(data, 10);
  auto bits = u16(data, 12);
  if (kind == '1' && bits > 32)
    bits = 8;
  if (image.width <= 0 || image.height <= 0 ||
      (bits != 4 && bits != 8 && bits != 24 && bits != 32))
    throw std::runtime_error("CZ: unsupported dimensions or color depth");
  const auto pixelBytes = size_t(image.width) * size_t(image.height) * 4;

  if (kind == '0') {
    if (headerLength + pixelBytes > data.size())
      throw std::runtime_error("CZ0: truncated pixels");
    image.rgba.assign(data.begin() + headerLength,
                      data.begin() + headerLength + pixelBytes);
    return image;
  }
  if (kind != '1' && kind != '3' && kind != '4')
    throw std::runtime_error("CZ: decoder supports CZ0/CZ1/CZ3/CZ4 only");
  size_t blockTable = headerLength;
  std::vector<std::array<std::uint8_t, 4>> palette;
  if (kind == '1' && (bits == 4 || bits == 8)) {
    const size_t colors = size_t(1) << bits;
    if (blockTable + colors * 4 > data.size())
      throw std::runtime_error("CZ1: truncated palette");
    palette.resize(colors);
    for (size_t i = 0; i < colors; ++i) {
      const auto p = blockTable + i * 4;
      palette[i] = {data[p + 2], data[p + 1], data[p], data[p + 3]};
    }
    blockTable += colors * 4;
  }
  if (blockTable + 4 > data.size())
    throw std::runtime_error("CZ3: truncated block table");

  const auto blockCount = u32(data, blockTable);
  size_t table = blockTable + 4;
  size_t compressed = table + size_t(blockCount) * 8;
  if (compressed > data.size())
    throw std::runtime_error("CZ3: invalid block table");
  std::vector<std::uint8_t> pixels;
  pixels.reserve(pixelBytes);
  for (std::uint32_t i = 0; i < blockCount; ++i) {
    const auto codes = u32(data, table + size_t(i) * 8);
    const auto raw = u32(data, table + size_t(i) * 8 + 4);
    auto block = decodeLzw(data, compressed, codes, raw);
    pixels.insert(pixels.end(), block.begin(), block.end());
  }
  const size_t rawBytes =
      kind == '1'
          ? (bits == 4
                 ? (size_t(image.width) * size_t(image.height) + 1) / 2
                 : size_t(image.width) * size_t(image.height) * (bits / 8))
          : pixelBytes;
  if (pixels.size() < rawBytes)
    throw std::runtime_error("CZ3: incomplete image");
  pixels.resize(rawBytes);

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

  const size_t stride = size_t(image.width) * 4;
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
  image.rgba = std::move(pixels);
  return image;
}

} // namespace LucaSystem
