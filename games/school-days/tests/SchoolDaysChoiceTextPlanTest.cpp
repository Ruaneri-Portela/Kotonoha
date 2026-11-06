#include <Kotonoha/routing/SchoolDaysChoiceTextPlan.hpp>
#include <Kotonoha/routing/SchoolDaysFontData.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

using Plan = Kotonoha::SchoolDaysChoiceTextPlan;

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SchoolDaysChoiceTextPlanTest: " << message << '\n';
    std::exit(2);
}
void Check(bool value, const std::string& message) {
    if (!value) Fail(message);
}

} // namespace

int main(int argc, char** argv) {
    Check(Plan::AdvanceFor(u'i') == 9 && Plan::AdvanceFor(u'j') == 9 &&
              Plan::AdvanceFor(u'l') == 9 && Plan::AdvanceFor(u'I') == 9,
          "narrow English advance drifted");
    Check(Plan::AdvanceFor(u'm') == 24 && Plan::AdvanceFor(u'w') == 24,
          "wide lowercase English advance drifted");
    Check(Plan::AdvanceFor(u'M') == 27 && Plan::AdvanceFor(u'W') == 27 &&
              Plan::AdvanceFor(u'Q') == 27,
          "wide uppercase English advance drifted");
    Check(Plan::AdvanceFor(u'A') == 20 && Plan::AdvanceFor(u'Z') == 20 &&
              Plan::AdvanceFor(u'a') == 16 && Plan::AdvanceFor(u' ') == 16,
          "base/uppercase English advance drifted");

    const std::u16string sample = u"I AM will";
    const auto glyphs = Plan::Build(sample);
    Check(glyphs.size() == sample.size() && !glyphs.empty() &&
              glyphs.front().x == 0 && glyphs.front().advance == 9 &&
              glyphs.back().x + glyphs.back().advance == Plan::Measure(sample),
          "choice glyph placement/measurement drifted");

    Check(Plan::ExitArgb(false, 0) == 0xFFF0F0F0u &&
              Plan::ExitAlpha(false, 1) == 245u &&
              Plan::ExitAlpha(false, 23) == 11u &&
              Plan::ExitAlpha(false, 24) == 0u,
          "nonselected 24-frame fade drifted");
    Check(Plan::ExitArgb(true, 0) == 0xFFFE4A1Fu &&
              Plan::ExitArgb(true, 23) == 0xFFFE4A1Fu &&
              Plan::ExitAlpha(true, 24) == 255u &&
              Plan::ExitAlpha(true, 25) == 245u &&
              Plan::ExitAlpha(true, 47) == 11u &&
              Plan::ExitAlpha(true, 48) == 0u,
          "selected hold+fade drifted");
    Check(Plan::ExitTailTicks(false) == 24u &&
              Plan::ExitTailTicks(true) == 48u,
          "choice physical-lifetime tail drifted");

    const std::u16string wrappedInput = std::u16string(64u, u'a') + u" bb";
    const auto wrapped = Plan::WrapRetailEnglish(wrappedInput);
    Check(wrapped.size() == 2u && wrapped[0].text.size() == 65u &&
              wrapped[0].text.back() == u' ' && wrapped[1].text == u"bb",
          "choice nominal 66-unit space-lookahead wrap drifted");
    Check(std::fabs(wrapped[0].horizontalOffset - (-524.0f)) < 0.001f &&
              std::fabs(wrapped[1].horizontalOffset - (-16.0f)) < 0.001f,
          "choice wrapped/final line centering offset drifted");
    const auto unbroken = Plan::WrapRetailEnglish(std::u16string(70u, u'a'));
    Check(unbroken.size() == 1u && unbroken[0].text.size() == 70u,
          "choice incorrectly hard-split an unbroken >66-unit word");

    const auto source0 = Plan::SourceForLine(0, 0);
    const auto source1 = Plan::SourceForLine(1, 2);
    Check(source0.x == 0.0f && source0.y == 0.0f && source0.w == 1024.0f &&
              source0.h == 48.0f && source1.y == 352.0f,
          "choice 1024x512 scratch source bands drifted");
    const auto one = Plan::DestinationForLine(1, 0, 0, 1, -8.0f);
    const auto top = Plan::DestinationForLine(2, 0, 0, 1, -8.0f);
    const auto bottom = Plan::DestinationForLine(2, 1, 0, 1, -8.0f);
    Check(std::fabs(one.y * 600.0f - 276.0f) < 0.001f &&
              std::fabs(top.y * 600.0f - 125.299988f) < 0.001f &&
              std::fabs(bottom.y * 600.0f - 426.0f) < 0.001f &&
              std::fabs(one.h - 0.08f) < 0.0001f,
          "choice normalized one/top/bottom geometry drifted");
    Check(std::fabs(one.y * 720.0f - 331.2f) < 0.001f &&
              std::fabs(top.y * 720.0f - 150.359985f) < 0.001f &&
              std::fabs(bottom.y * 720.0f - 511.2f) < 0.001f &&
              std::fabs(one.h * 720.0f - 57.6f) < 0.001f &&
              std::fabs(one.y * 576.0f - 264.96f) < 0.001f &&
              std::fabs(top.y * 576.0f - 120.287988f) < 0.001f &&
              std::fabs(bottom.y * 576.0f - 408.96f) < 0.001f,
          "choice native Full/Note transform drifted");
    Check(Plan::CompositeScratchPixel(0x10FFFFFFu, 0x20000000u) ==
              0x20000000u &&
              Plan::CompositeScratchPixel(0x20A0A0A0u, 0x20808080u) ==
              0x20A0A0A0u,
          "choice packed-ARGB scratch overlap compositor drifted");

    if (argc >= 2) {
        fs::path fontPath = fs::path(argv[1]) / "System" / "System" /
                            "FONTDATA_ENG.DAT";
        Kotonoha::SchoolDaysFontData font;
        std::string error;
        Check(font.LoadFile(fontPath.string(), &error), error);
        for (const char16_t unit : sample) {
            Check(font.HasGlyph(static_cast<uint16_t>(unit)),
                  "authenticated FONTDATA_ENG lacks sample choice glyph");
        }
    }

    std::cout << "SCHOOL DAYS CHOICE TEXT PLAN GATE PASS\n"
              << "fontdata_cell_48x48=PASS\n"
              << "english_advance_adjustments=PASS\n"
              << "glyph_placement_measurement=PASS\n"
              << "normal_selected_colors=PASS\n"
              << "native_integer_fade_24_48=PASS\n"
              << "retail_66unit_space_wrap=PASS\n"
              << "retail_normalized_geometry=PASS\n"
              << "packed_argb_overlap_max=PASS\n"
              << "optional_fontdata_eng_corpus=PASS\n";
    return 0;
}
