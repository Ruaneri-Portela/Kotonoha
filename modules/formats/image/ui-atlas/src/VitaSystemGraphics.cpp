// Vita UI/font formats from hokagocafe/open-amagami (MIT).
// Licence notice: third_party/open-amagami-LICENSE.txt.
#include <Amagami/VitaSystemGraphics.hpp>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Amagami::Vita {
namespace {
uint16_t le16(std::string_view bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 2)
    throw std::runtime_error("Amagami Vita: truncated 16-bit field");
  return uint16_t(uint8_t(bytes[offset])) |
         (uint16_t(uint8_t(bytes[offset + 1])) << 8);
}
uint32_t le32(std::string_view bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    throw std::runtime_error("Amagami Vita: truncated 32-bit field");
  return uint32_t(uint8_t(bytes[offset])) |
         (uint32_t(uint8_t(bytes[offset + 1])) << 8) |
         (uint32_t(uint8_t(bytes[offset + 2])) << 16) |
         (uint32_t(uint8_t(bytes[offset + 3])) << 24);
}
void checkElf(std::string_view elf) {
  if (elf.size() < 52 ||
      std::memcmp(elf.data(),
                  "\x7f"
                  "ELF",
                  4) ||
      uint8_t(elf[4]) != 1 || uint8_t(elf[5]) != 1)
    throw std::runtime_error(
        "Amagami Vita: expected a 32-bit little-endian ELF");
  const uint32_t phoff = le32(elf, 28);
  const uint16_t phsize = le16(elf, 42), phcount = le16(elf, 44);
  if (phsize < 32 || uint64_t(phoff) + uint64_t(phsize) * phcount > elf.size())
    throw std::runtime_error("Amagami Vita: invalid ELF program headers");
}
int glyphIndex(uint16_t sjis) {
  const int hi = sjis >> 8, lo = sjis & 0xff;
  if (!((hi >= 0x81 && hi <= 0x9f) || (hi >= 0xe0 && hi <= 0xef)) ||
      !((lo >= 0x40 && lo <= 0x7e) || (lo >= 0x80 && lo <= 0xfc)))
    throw std::runtime_error("Amagami Vita: invalid two-byte Shift-JIS glyph");
  const int row = hi <= 0x9f ? hi - 0x81 : hi - 0xe0 + 31;
  return row * 188 + (lo <= 0x7e ? lo - 0x40 : lo - 0x41);
}
Picture indexedWithPitch(std::string_view bytes, uint32_t width,
                         uint32_t height, uint32_t pitch) {
  const uint64_t pixels = uint64_t(width) * height;
  if (!width || !height || width > 4096 || height > 4096 || pitch < width ||
      pitch > 8192 || pixels > 4u * 1024 * 1024 || bytes.size() < 1024 ||
      uint64_t(pitch) * height > bytes.size() - 1024)
    throw std::runtime_error("Amagami Vita: invalid UI texture dimensions");
  Picture image;
  image.width = width;
  image.height = height;
  image.rgba.resize(size_t(pixels) * 4);
  std::array<std::array<uint8_t, 4>, 256> lut{};
  for (unsigned i = 0; i < 256; ++i)
    for (unsigned c = 0; c < 4; ++c)
      lut[i][c] = uint8_t(bytes[4 * i + c]); // UI palette is RGBA.
  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x) {
      const auto &colour = lut[uint8_t(bytes[1024 + size_t(y) * pitch + x])];
      const size_t out = (size_t(y) * width + x) * 4;
      for (unsigned c = 0; c < 4; ++c)
        image.rgba[out + c] = colour[c];
    }
  return image;
}
} // namespace

Picture decodeFontGlyph(std::string_view bytes, uint16_t shiftJis,
                        unsigned palette) {
  constexpr unsigned rowBytes = 512, glyphWidth = 36, glyphHeight = 42;
  if (palette >= 4 || bytes.size() < 1024 + rowBytes * glyphHeight ||
      (bytes.size() - 1024) % rowBytes != 0 ||
      bytes.size() > 1024 + rowBytes * 8192)
    throw std::runtime_error("Amagami Vita: invalid font atlas or palette");
  const uint32_t rowCount = uint32_t((bytes.size() - 1024) / rowBytes);
  const int glyph = glyphIndex(shiftJis);
  const uint32_t x0 = (unsigned(glyph) % 28) * glyphWidth;
  const uint32_t y0 = (unsigned(glyph) / 28) * glyphHeight;
  if (y0 + glyphHeight > rowCount)
    throw std::runtime_error("Amagami Vita: glyph outside font atlas");
  Picture image;
  image.width = glyphWidth;
  image.height = glyphHeight;
  image.rgba.resize(size_t(glyphWidth) * glyphHeight * 4);
  for (uint32_t y = 0; y < glyphHeight; ++y)
    for (uint32_t x = 0; x < glyphWidth; ++x) {
      const uint32_t sourceX = x0 + x;
      const uint8_t packed =
          uint8_t(bytes[1024 + size_t(y0 + y) * rowBytes + sourceX / 2]);
      const uint8_t colour = sourceX & 1 ? packed >> 4 : packed & 15;
      const size_t source = 4 * (size_t(palette) * 16 + colour);
      const size_t target = (size_t(y) * glyphWidth + x) * 4;
      for (unsigned c = 0; c < 4; ++c)
        image.rgba[target + c] = uint8_t(bytes[source + c]);
    }
  return image;
}

UiAtlas::UiAtlas(std::string elf, std::string sysgraph)
    : elf_(std::move(elf)), sysgraph_(std::move(sysgraph)) {
  checkElf(elf_);
  if (sysgraph_.size() < 762 * 4)
    throw std::runtime_error("Amagami Vita: incomplete sysgraph offset table");

  // The English patch v1.1 independently shifts the record and size tables.
  const uint32_t phoff = le32(elf_, 28);
  const uint16_t stride = le16(elf_, 42), count = le16(elf_, 44);
  bool text = false, data = false;
  for (uint16_t i = 0; i < count; ++i) {
    const size_t at = size_t(phoff) + size_t(stride) * i;
    if (le32(elf_, at) != 1)
      continue; // PT_LOAD
    const uint32_t va = le32(elf_, at + 8);
    const uint32_t fileSize = le32(elf_, at + 16);
    const uint32_t memSize = le32(elf_, at + 20);
    text |= va == 0x81000000 && fileSize == 0xc9254 && memSize == 0xc9254;
    data |= va == 0x810ca000 && fileSize == 0x15b1c && memSize == 0x1fe368;
  }
  patched_ = text && data;
}

uint32_t UiAtlas::tableVa(uint32_t original) const {
  if (!patched_)
    return original;
  if (original == 0x810ddf94)
    return original + 4; // texture pointers
  if (original >= 0x810ac158 && original < 0x810ac158 + 762 * 8)
    return original + 0x230; // entry records
  if (original >= 0x810adf44 && original < 0x810adf44 + 762 * 16)
    return original + 0x230; // per-entry sizes
  return original;
}

size_t UiAtlas::fileOffset(uint32_t virtualAddress, size_t bytes) const {
  const uint32_t phoff = le32(elf_, 28);
  const uint16_t stride = le16(elf_, 42), count = le16(elf_, 44);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t at = size_t(phoff) + size_t(stride) * i;
    if (le32(elf_, at) != 1)
      continue;
    const uint32_t offset = le32(elf_, at + 4);
    const uint32_t address = le32(elf_, at + 8);
    const uint32_t length = le32(elf_, at + 16);
    if (virtualAddress < address)
      continue;
    const uint64_t distance = uint64_t(virtualAddress) - address;
    if (distance > length || bytes > length - distance)
      continue;
    const uint64_t position = uint64_t(offset) + distance;
    if (position > elf_.size() || bytes > elf_.size() - position)
      continue;
    return size_t(position);
  }
  throw std::runtime_error("Amagami Vita: ELF address outside loaded segment");
}
uint32_t UiAtlas::word(uint32_t virtualAddress) const {
  return le32(elf_, fileOffset(virtualAddress, 4));
}

std::optional<Picture> UiAtlas::decodeEntry(uint32_t entry) const {
  if (entry == 0)
    return std::nullopt;
  if (entry >= 762)
    throw std::runtime_error("Amagami Vita: sysgraph entry out of range");
  const uint32_t offset = le32(sysgraph_, size_t(entry) * 4);
  const uint64_t info = uint64_t(tableVa(0x810adf44)) + 16ull * entry;
  if (info > std::numeric_limits<uint32_t>::max() - 12)
    throw std::runtime_error("Amagami Vita: entry info address overflows");
  const uint32_t width = word(uint32_t(info + 4));
  const uint32_t height = word(uint32_t(info + 8));
  const uint32_t pitch = word(uint32_t(info + 12));
  if (offset > sysgraph_.size() || sysgraph_.size() - offset < 1024)
    throw std::runtime_error("Amagami Vita: UI resource offset out of range");
  return indexedWithPitch(std::string_view(sysgraph_).substr(offset), width,
                          height, pitch);
}

std::optional<Picture> UiAtlas::decode(uint32_t textureId,
                                       uint32_t variant) const {
  if (textureId >= 337 || variant > 255)
    throw std::runtime_error("Amagami Vita: UI texture selector out of range");
  const uint64_t table = uint64_t(tableVa(0x810ddf94)) + 4ull * textureId;
  if (table > std::numeric_limits<uint32_t>::max())
    throw std::runtime_error("Amagami Vita: texture table overflows");
  const uint32_t variants = word(uint32_t(table));
  if (!variants)
    return std::nullopt;

  const uint32_t records = tableVa(0x810ac158);
  const auto getRecord = [&](uint32_t v) -> uint32_t {
    if (variants > std::numeric_limits<uint32_t>::max() - 4 * v)
      throw std::runtime_error("Amagami Vita: variant address overflows");
    const uint32_t pointer = word(variants + 4 * v);
    if (pointer < records || uint64_t(pointer) >= uint64_t(records) + 762 * 8 ||
        (pointer - records) % 8)
      return 0;
    return pointer;
  };
  uint32_t record = getRecord(variant);
  if (!record)
    record = getRecord(0);
  if (!record)
    return std::nullopt;
  return decodeEntry(word(record));
}
} // namespace Amagami::Vita
