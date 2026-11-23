#include <Amagami/VitaSystemGraphics.hpp>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void check(bool value, const char *why) {
  if (!value)
    throw std::runtime_error(why);
}
template <class Callback> void rejects(Callback fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  check(rejected, "Expected invalid Vita data to be rejected");
}
void put16(std::string &data, size_t at, uint16_t value) {
  data.at(at) = char(value);
  data.at(at + 1) = char(value >> 8);
}
void put32(std::string &data, size_t at, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    data.at(at + i) = char(value >> (i * 8));
}
std::string sampleFont() {
  constexpr size_t rowBytes = 512;
  std::string font(1024 + rowBytes * 42, '\0'); // one glyph row
  // Palette 2, index 1 is red, alpha 64; palette 3, index 1 is green.
  font[4 * (2 * 16 + 1) + 0] = char(222);
  font[4 * (2 * 16 + 1) + 3] = char(64);
  font[4 * (3 * 16 + 1) + 1] = char(111);
  // Shift-JIS 0x8140 maps to the first glyph, low nibble on the left.
  font[1024] = char(0x21);
  return font;
}
std::string sampleElf() {
  const uint32_t base = 0x81000000, length = 0x120000;
  std::string elf(0x100 + length, '\0');
  elf.replace(0, 4,
              "\x7f"
              "ELF",
              4);
  elf[4] = 1;
  elf[5] = 1;
  put32(elf, 28, 52); // 32-bit program-header table
  put16(elf, 42, 32);
  put16(elf, 44, 1);
  put32(elf, 52, 1); // PT_LOAD
  put32(elf, 56, 0x100);
  put32(elf, 60, base);
  put32(elf, 68, length);
  put32(elf, 72, length);
  const auto offset = [](uint32_t va) -> size_t {
    return 0x100 + size_t(va - 0x81000000);
  };
  // texture 2 -> variant table, variant 3 -> entry 1, variant 0 -> entry 2
  constexpr uint32_t variants = 0x810b1000, records = 0x810ac158;
  put32(elf, offset(0x810ddf94 + 2 * 4), variants);
  put32(elf, offset(variants), records + 16);
  put32(elf, offset(variants + 3 * 4), records + 8);
  put32(elf, offset(records + 8), 1);
  put32(elf, offset(records + 16), 2);
  const uint32_t sizes = 0x810adf44;
  for (uint32_t e = 1; e <= 2; ++e) {
    put32(elf, offset(sizes + 16 * e + 4), 2);  // visible width
    put32(elf, offset(sizes + 16 * e + 8), 2);  // visible height
    put32(elf, offset(sizes + 16 * e + 12), 3); // padded row pitch
  }
  return elf;
}
std::string sampleSysgraph() {
  std::string image(5500, '\0');
  put32(image, 4, 3100); // entry 1
  put32(image, 8, 4300); // entry 2
  for (const size_t at : {size_t(3100), size_t(4300)}) {
    image[at + 4] = char(19);
    image[at + 5] = char(23);
    image[at + 6] = char(41);
    image[at + 7] = char(173);
    image[at + 1024] = 1;
    image[at + 1025] = 0;
    image[at + 1026] =
        static_cast<char>(255); // padding index must not enter visible pixels
    image[at + 1027] = 0;
    image[at + 1028] = 1;
  }
  return image;
}
} // namespace

int main() {
  try {
    using namespace Amagami::Vita;
    const auto font = sampleFont();
    const Picture red = decodeFontGlyph(font, 0x8140, 2);
    const Picture green = decodeFontGlyph(font, 0x8140, 3);
    check(red.width == 36 && red.height == 42 && red.rgba.size() == 36 * 42 * 4,
          "Vita font glyph dimensions");
    check(red.rgba[0] == 222 && red.rgba[3] == 64 && red.rgba[4] == 0,
          "Vita font: low nibble before high nibble and correct palette");
    check(green.rgba[0] == 0 && green.rgba[1] == 111,
          "Vita font alternative colour palette");
    rejects([&] { decodeFontGlyph(font, 0x8140, 4); });
    rejects([&] { decodeFontGlyph(font, 0x817f, 0); });
    rejects([&] { decodeFontGlyph(font, 0x8240, 1); }); // second glyph row
    rejects(
        [&] { decodeFontGlyph(font.substr(0, font.size() - 1), 0x8140, 0); });

    UiAtlas atlas(sampleElf(), sampleSysgraph());
    const auto entry = atlas.decode(2, 3);
    check(entry && entry->width == 2 && entry->height == 2 &&
              entry->rgba.size() == 16,
          "Vita sysgraph texture dimensions");
    check(entry->rgba[0] == 19 && entry->rgba[1] == 23 &&
              entry->rgba[2] == 41 && entry->rgba[3] == 173 &&
              entry->rgba[4] == 0 && entry->rgba[8] == 0 &&
              entry->rgba[12] == 19,
          "Vita UI RGBA palette and padded row pitch");
    check(atlas.decode(2, 100) && atlas.decode(2, 100)->width == 2,
          "Missing variant falls back to variant zero");
    check(!atlas.decode(0, 0) && !atlas.decodeEntry(0),
          "Unassigned UI ids and empty entry");
    rejects([&] { atlas.decode(337, 0); });
    rejects([&] { atlas.decode(2, 256); });
    rejects([&] { atlas.decodeEntry(762); });
    rejects([&] { UiAtlas bad("not ELF", sampleSysgraph()); });
    rejects([&] { UiAtlas bad(sampleElf(), std::string(8, '\0')); });

    auto broken = sampleSysgraph();
    put32(broken, 4, uint32_t(broken.size()));
    UiAtlas offsetError(sampleElf(), std::move(broken));
    rejects([&] { offsetError.decodeEntry(1); });
    std::cout << "Vita font and sysgraph synthetic tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
