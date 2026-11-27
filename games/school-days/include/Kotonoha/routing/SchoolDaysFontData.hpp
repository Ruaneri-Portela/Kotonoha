#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

// Safe reader for the authenticated School Days HQ FONTDATA raster format.
// The shipped runtime indexes a fixed 65536-entry uint32-LE offset table by
// UTF-16 code unit.  Each nonzero offset points at one RLE-compressed 48x48
// grayscale/alpha glyph record.
class SchoolDaysFontData {
public:
    static constexpr uint32_t kGlyphCount = 65536u;
    static constexpr uint32_t kTableBytes = kGlyphCount * 4u;
    static constexpr int kGlyphWidth = 48;
    static constexpr int kGlyphHeight = 48;
    static constexpr int kGlyphPixels = kGlyphWidth * kGlyphHeight;

    struct Glyph {
        std::array<uint8_t, kGlyphPixels> gray{};
        std::array<uint8_t, kGlyphPixels> alpha{};
    };

    bool LoadFile(const std::string& path, std::string* error = nullptr);
    bool Parse(std::vector<uint8_t> bytes, std::string* error = nullptr);
    void Clear();

    bool IsLoaded() const { return !bytes_.empty(); }
    bool HasGlyph(uint16_t glyph) const {
        return offsets_.size() == kGlyphCount && offsets_[glyph] != 0u;
    }
    bool DecodeGlyph(uint16_t glyph, Glyph* out,
                     std::string* error = nullptr) const;
    size_t Size() const { return bytes_.size(); }
    size_t NonzeroGlyphCount() const { return nonzeroGlyphs_; }

private:
    bool DecodeRecord(uint32_t start, uint32_t end, Glyph* out,
                      std::string* error) const;

    std::vector<uint8_t> bytes_;
    std::vector<uint32_t> offsets_;
    std::vector<uint32_t> recordEnds_;
    size_t nonzeroGlyphs_ = 0u;
};

} // namespace Kotonoha
