#include <Kotonoha/routing/SchoolDaysFontData.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Font = Kotonoha::SchoolDaysFontData;

[[noreturn]] void Fail(const char* text) {
    std::cerr << "SchoolDaysFontDataTest: " << text << '\n';
    std::exit(2);
}
void Check(bool value, const char* text) { if (!value) Fail(text); }

void PutU32(std::vector<uint8_t>* bytes, size_t at, uint32_t value) {
    (*bytes)[at + 0] = static_cast<uint8_t>(value);
    (*bytes)[at + 1] = static_cast<uint8_t>(value >> 8u);
    (*bytes)[at + 2] = static_cast<uint8_t>(value >> 16u);
    (*bytes)[at + 3] = static_cast<uint8_t>(value >> 24u);
}
}

int main(int argc, char** argv) {
    std::string error;
    std::vector<uint8_t> fixture(Font::kTableBytes + 3u, 0u);
    PutU32(&fixture, 0u, Font::kTableBytes);
    fixture[Font::kTableBytes + 0u] = 0x81u;
    fixture[Font::kTableBytes + 1u] = 0x2au;
    fixture[Font::kTableBytes + 2u] = 0u;

    Font font;
    Check(font.Parse(fixture, &error), error.c_str());
    Check(font.NonzeroGlyphCount() == 1u && font.HasGlyph(0),
          "synthetic offset table mismatch");
    Font::Glyph glyph;
    Check(font.DecodeGlyph(0, &glyph, &error), error.c_str());
    for (int i = 0; i < 3; ++i) {
        Check(glyph.gray[static_cast<size_t>(i)] == 2u &&
                  glyph.alpha[static_cast<size_t>(i)] == 170u,
              "visible RLE run mismatch");
    }
    Check(glyph.alpha[3] == 0u, "implicit transparent tail mismatch");

    auto bad = fixture;
    bad[Font::kTableBytes + 2u] = 1u;
    Check(!font.Parse(std::move(bad), &error), "missing final terminator accepted");

    auto trailing = fixture;
    trailing.push_back(0xaau);
    trailing.push_back(0x55u);
    Check(font.Parse(std::move(trailing), &error), error.c_str());
    Check(font.Size() == Font::kTableBytes + 3u,
          "FONTDATA trailing extractor bytes were treated as logical payload");
    Check(font.DecodeGlyph(0, &glyph, &error), error.c_str());

    if (argc == 3) {
        Font jp;
        Font en;
        Check(jp.LoadFile(argv[1], &error), error.c_str());
        Check(en.LoadFile(argv[2], &error), error.c_str());
        Check(jp.Size() == 42011567u && jp.NonzeroGlyphCount() == 22421u,
              "authenticated JP FONTDATA corpus mismatch");
        Check(en.Size() == 39860318u && en.NonzeroGlyphCount() == 22836u,
              "authenticated EN FONTDATA corpus mismatch");
        Check(jp.DecodeGlyph(UINT16_C(0x65e5), &glyph, &error),
              error.c_str());
        Check(en.DecodeGlyph(UINT16_C(0x0041), &glyph, &error),
              error.c_str());
        Check(jp.DecodeGlyph(UINT16_C(0xffee), &glyph, &error),
              "authenticated JP final glyph failed to decode");
        Check(en.DecodeGlyph(UINT16_C(0xffee), &glyph, &error),
              "authenticated EN final glyph failed to decode");
    }

    std::cout << "SCHOOL DAYS FONTDATA GATE PASS\n"
              << "fixed_65536_offset_table=PASS\n"
              << "rle_gray_alpha=PASS\n"
              << "strict_record_bounds=PASS\n"
              << "authenticated_corpus=PASS\n";
    return 0;
}
