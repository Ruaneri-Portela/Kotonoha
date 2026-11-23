#pragma once
// Vita's font and UI atlas formats, based on MIT-licensed open-amagami research.
// The graphics are game data only: no retail asset or executable is distributed.
#include <Amagami/VitaGraphics.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Amagami::Vita {

// Reads one 36x42 Shift-JIS glyph from the Vita 4bpp font atlas.
// The 1024-byte prefix stores four RGBA palettes of 16 colours (first 256
// bytes used), followed by 1024-pixel rows packed low-nibble first.
Picture decodeFontGlyph(std::string_view fontData, uint16_t shiftJis,
                        unsigned palette);

// Reads UI texture/variant tables from an unwrapped, ordinary 32-bit ARM ELF.
// The "system/sysgraph.tm2" bytes are an already-decompressed CPK member.
// Neither Vita's SELF executable wrapping nor copy protection is handled here.
class UiAtlas {
public:
  UiAtlas(std::string elf, std::string sysgraph);

  // Nullopt for an absent texture or entry zero. Throws for invalid file data.
  std::optional<Picture> decode(uint32_t textureId, uint32_t variant) const;
  std::optional<Picture> decodeEntry(uint32_t entry) const;

private:
  size_t fileOffset(uint32_t virtualAddress, size_t bytes) const;
  uint32_t word(uint32_t virtualAddress) const;
  uint32_t tableVa(uint32_t original) const;

  std::string elf_;
  std::string sysgraph_;
  bool patched_ = false;
};

} // namespace Amagami::Vita
