#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {

// Renderer-neutral v1.02 SetSELECT text contract.  Glyph pixels come from the
// existing SchoolDaysFontData decoder; this plan owns only authenticated cell
// geometry, English advances and commit-fade colors.
class SchoolDaysChoiceTextPlan final {
public:
    static constexpr int kGlyphWidth = 48;
    static constexpr int kGlyphHeight = 48;
    // FUN_0044CA10 seeds 36 for the generic path, then retail UseEnglish=1
    // overwrites the per-glyph base to 16 before applying FUN_0044C660's
    // English adjustment table.
    static constexpr int kBaseAdvance = 16;
    static constexpr uint32_t kNormalRgb = 0x00F0F0F0u;
    static constexpr uint32_t kSelectedRgb = 0x00FE4A1Fu;
    static constexpr uint32_t kNormalArgb = 0xFFF0F0F0u;
    static constexpr uint32_t kSelectedArgb = 0xFFFE4A1Fu;
    static constexpr uint32_t kRetailFrameRate = 24u;

    struct GlyphPlacement {
        uint16_t glyph = 0;
        int x = 0;
        int advance = 0;
    };

    struct Line {
        std::u16string text;
        std::vector<GlyphPlacement> glyphs;
        int nominalUnits = 0;
        float horizontalOffset = 0.0f;
    };

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    // Retail UseEnglish=1 overrides the generic mode-16 seed with base 16 and
    // applies these exact English adjustments before advancing to the next glyph.
    static int AdvanceFor(uint16_t glyph);
    static int Measure(const std::u16string& text);
    static std::vector<GlyphPlacement> Build(const std::u16string& text);
    static std::vector<Line> WrapRetailEnglish(const std::u16string& text);
    static Rect SourceForLine(int choiceIndex, int lineIndex);
    static Rect DestinationForLine(int choiceCount, int choiceIndex,
                                   int lineIndex, int lineCount,
                                   float horizontalOffset);
    static uint32_t CompositeScratchPixel(uint32_t existing,
                                          uint32_t incoming) {
        return incoming > existing ? incoming : existing;
    }
    static constexpr uint32_t ExitTailTicks(bool selected) {
        return selected ? 48u : 24u;
    }

    // Exact state2/state3 arithmetic from EXE 0x44D11D..0x44D25D.
    // elapsedFrames is unsigned native frame delta since choice commit.
    static uint8_t ExitAlpha(bool selected, uint32_t elapsedFrames,
                             uint32_t frameRate = kRetailFrameRate);
    static uint32_t ExitArgb(bool selected, uint32_t elapsedFrames,
                             uint32_t frameRate = kRetailFrameRate);
};

} // namespace Kotonoha
