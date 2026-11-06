#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>

#include <utility>

namespace Kotonoha {

int SchoolDaysChoiceTextPlan::AdvanceFor(uint16_t glyph) {
    switch (glyph) {
    case u'i':
    case u'j':
    case u'l':
    case u'I':
        return kBaseAdvance - 7;
    case u'm':
    case u'w':
        return kBaseAdvance + 8;
    case u'M':
    case u'W':
    case u'Q':
        return kBaseAdvance + 11;
    default:
        if (glyph >= u'A' && glyph <= u'Z') return kBaseAdvance + 4;
        return kBaseAdvance;
    }
}

int SchoolDaysChoiceTextPlan::Measure(const std::u16string& text) {
    int width = 0;
    for (const char16_t glyph : text)
        width += AdvanceFor(static_cast<uint16_t>(glyph));
    return width;
}

std::vector<SchoolDaysChoiceTextPlan::GlyphPlacement>
SchoolDaysChoiceTextPlan::Build(const std::u16string& text) {
    std::vector<GlyphPlacement> out;
    out.reserve(text.size());
    int x = 0;
    for (const char16_t unit : text) {
        const uint16_t glyph = static_cast<uint16_t>(unit);
        const int advance = AdvanceFor(glyph);
        out.push_back({glyph, x, advance});
        x += advance;
    }
    return out;
}

std::vector<SchoolDaysChoiceTextPlan::Line>
SchoolDaysChoiceTextPlan::WrapRetailEnglish(const std::u16string& text) {
    constexpr int kNominalLimit = 66;
    constexpr float kHalfAdvance = 8.0f;
    std::vector<Line> out;
    std::u16string current;
    current.reserve(text.size());

    const auto flush = [&](bool wrapped) {
        if (current.empty()) return;
        Line line;
        line.text = current;
        line.nominalUnits = static_cast<int>(current.size());
        line.horizontalOffset =
            -(kHalfAdvance * static_cast<float>(line.nominalUnits) +
              (wrapped ? 4.0f : 0.0f));
        line.glyphs = Build(line.text);
        out.push_back(std::move(line));
        current.clear();
    };

    for (size_t i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i];
        current.push_back(unit);
        if (unit != u' ') continue;

        size_t next = i + 1u;
        int nextWord = 0;
        while (next < text.size() && text[next] != u' ') {
            ++nextWord;
            ++next;
        }
        if (static_cast<int>(current.size()) + nextWord > kNominalLimit)
            flush(true);
    }
    flush(false);
    if (out.empty()) out.push_back(Line{});
    return out;
}

SchoolDaysChoiceTextPlan::Rect
SchoolDaysChoiceTextPlan::SourceForLine(int choiceIndex, int lineIndex) {
    if (choiceIndex < 0 || choiceIndex > 1 || lineIndex < 0 || lineIndex >= 5)
        return {};
    return {0.0f,
            static_cast<float>(choiceIndex * 256 + lineIndex * kGlyphHeight),
            1024.0f, static_cast<float>(kGlyphHeight)};
}

SchoolDaysChoiceTextPlan::Rect
SchoolDaysChoiceTextPlan::DestinationForLine(int choiceCount, int choiceIndex,
                                             int lineIndex, int lineCount,
                                             float horizontalOffset) {
    if ((choiceCount != 1 && choiceCount != 2) || choiceIndex < 0 ||
        choiceIndex >= choiceCount || lineIndex < 0 || lineCount <= 0 ||
        lineIndex >= lineCount) {
        return {};
    }

    constexpr float kCanvasWidth = 1066.6666667f;
    constexpr float kCanvasHeight = 600.0f;
    constexpr float kBaseX = 533.4000244f;
    constexpr float kQuadWidth = 1066.8000488f;
    float anchor = 300.0f;
    if (choiceCount == 2)
        anchor = choiceIndex == 0 ? 149.299988f : 450.0f;
    const float y = anchor - static_cast<float>(lineCount) * 24.0f +
                    static_cast<float>(lineIndex) * 48.0f;
    return {(kBaseX + horizontalOffset) / kCanvasWidth,
            y / kCanvasHeight,
            kQuadWidth / kCanvasWidth,
            48.0f / kCanvasHeight};
}

uint8_t SchoolDaysChoiceTextPlan::ExitAlpha(bool selected,
                                            uint32_t elapsedFrames,
                                            uint32_t frameRate) {
    if (frameRate == 0u) return 0u;
    if (selected) {
        if (elapsedFrames < frameRate) return 0xffu;
        if (elapsedFrames >= frameRate * 2u) return 0u;
        elapsedFrames -= frameRate;
    } else if (elapsedFrames >= frameRate) {
        return 0u;
    }

    // Native integer path: 255 - ((elapsed * 255) / FrameRate).
    return static_cast<uint8_t>(
        0xffu - static_cast<uint32_t>(
                    (static_cast<uint64_t>(elapsedFrames) * 0xffu) / frameRate));
}

uint32_t SchoolDaysChoiceTextPlan::ExitArgb(bool selected,
                                            uint32_t elapsedFrames,
                                            uint32_t frameRate) {
    const uint32_t alpha = ExitAlpha(selected, elapsedFrames, frameRate);
    const uint32_t rgb = selected ? kSelectedRgb : kNormalRgb;
    return (alpha << 24u) | rgb;
}

} // namespace Kotonoha
