// Apple-only text codec tests: no SDL, libiconv, game assets or fonts needed.
// Compile Encoding.cpp alongside Encoding.mm to catch duplicate definitions.
#include <Kotonoha/localization/Encoding.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <iostream>

namespace {
void expect(bool condition, const char *why) {
  if (!condition)
    throw std::runtime_error(why);
}
void equal(const std::string &actual, const std::string &wanted,
           const char *why) {
  expect(actual == wanted, why);
}
void roundTrip(std::string_view bytes, const char *encoding, const char *why) {
  try {
    const auto text = Kotonoha::ConvertEncoding(bytes, encoding, "UTF-8");
    equal(Kotonoha::ConvertEncoding(text, "UTF-8", encoding),
          std::string(bytes), why);
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string(why) + ": " + e.what());
  }
}
} // namespace

int main() {
  using Kotonoha::ConvertEncoding;
  const char *stage = "CP932";
  try {
    // Real game content: JIS hiragana, an NEC symbol, Windows CP932 wave dash,
    // half-width Katakana, ASCII. This requires Windows-31J (not plain JIS).
    const std::string cp932 = "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd"
                              "\x87\x40\x81\x60\xb1" "A";
    equal(ConvertEncoding(cp932, "CP932", "UTF-8"),
          u8"こんにちは①～ｱA", "Windows CP932 extensions decode correctly");
    equal(Kotonoha::ShiftJisToUtf8(cp932), u8"こんにちは①～ｱA",
          "ShiftJisToUtf8 uses the shared Apple codec");
    roundTrip(cp932, "windows-31j", "Windows-31J full round trip");
    roundTrip(cp932, "Shift_JIS", "Shift_JIS must use CP932 compatibility");
    equal(ConvertEncoding(u8"～", "UTF-8", "CP932"),
          std::string("\x81\x60", 2), "Windows wave dash byte mapping");
    equal(ConvertEncoding(std::string("a\x85\x40" "z", 4), "CP932"), "a?z",
          "Unmapped CP932 pair must be consumed once");
    equal(ConvertEncoding(std::string("a\x81" "!z", 4), "CP932"), "a?!z",
          "Bad CP932 trail does not eat the next ASCII character");

    stage = "Western CP1252/Latin1";
    equal(ConvertEncoding("caf\xe9", "LATIN1"), u8"café",
          "ISO-8859-1/Latin1 decoding");
    roundTrip("caf\xe9", "ISO-8859-1", "Latin1 round trip");
    equal(ConvertEncoding("caf\xe9 \x80", "CP1252"), u8"café €",
          "Windows-1252 decoding with euro sign");
    equal(ConvertEncoding(u8"€", "UTF-8", "windows-1252"),
          std::string("\x80", 1), "CP1252 encode euro sign");
    equal(ConvertEncoding(std::string("\x81", 1), "CP1252"), "?",
          "Undefined Windows-1252 input uses '?'");
    equal(ConvertEncoding(u8"🙂", "UTF-8", "CP1252"), "?",
          "Unrepresentable character uses target '?'");
    equal(ConvertEncoding(std::string("a\xff" "z", 3), "utf-8"), "a?z",
          "Malformed UTF-8 cannot corrupt neighbors");

    stage = "UTF-8/UTF-16 scalar";
    const std::string utf8 = u8"A🙂日";
    auto utf16 = Kotonoha::Utf8ToUtf16(utf8);
    expect(utf16 == u"A🙂日", "UTF-8 to native UTF-16 surrogate pairs");
    equal(Kotonoha::Utf16ToUtf8(utf16), utf8,
          "Native UTF-16 to UTF-8 supplementary text");
    expect(Kotonoha::Utf8ToUtf16(std::string("\xff", 1)).empty(),
           "Invalid UTF-8 cannot be read as native UTF-16");
    const std::string nul("A\0B", 3);
    equal(ConvertEncoding(nul, "UTF-8", "UTF-8"), nul,
          "Embedded NUL must not truncate binary text");

    stage = "UTF-16 binary formats";
    const std::string le16("\x41\x00\x3d\xd8\x42\xde", 6);
    const std::string be16("\x00\x41\xd8\x3d\xde\x42", 6);
    equal(ConvertEncoding(le16, "UTF-16LE"), u8"A🙂",
          "UTF-16LE supplementary decode");
    equal(ConvertEncoding(be16, "UTF-16BE"), u8"A🙂",
          "UTF-16BE/Joliet supplementary decode");
    equal(ConvertEncoding(u8"A🙂", "UTF-8", "UTF-16LE"), le16,
          "UTF-16LE byte output without BOM");
    equal(ConvertEncoding(u8"A🙂", "UTF-8", "UTF-16BE"), be16,
          "UTF-16BE byte output without BOM");

    stage = "UTF-32 binary formats";
    const std::string le32("\x41\x00\x00\x00\x42\xf6\x01\x00", 8);
    const std::string be32("\x00\x00\x00\x41\x00\x01\xf6\x42", 8);
    equal(ConvertEncoding(le32, "UTF-32LE"), u8"A🙂",
          "UTF-32LE BOM-asset decode");
    equal(ConvertEncoding(be32, "UTF-32BE"), u8"A🙂",
          "UTF-32BE BOM-asset decode");
    equal(ConvertEncoding(u8"A🙂", "UTF-8", "UTF-32LE"), le32,
          "UTF-32LE byte output");
    equal(ConvertEncoding(u8"A🙂", "UTF-8", "UTF-32BE"), be32,
          "UTF-32BE byte output");
    equal(ConvertEncoding(std::string("\x00\xd8", 2), "UTF-16LE"), "?",
          "Broken UTF-16 surrogate recovered");

    stage = "Debug overlay encodings";
    // The DebugOverlay exposes the following additional encoding families.
    roundTrip("\xc6\xfc\xcb\xdc", "EUC-JP",
              "EUC-JP Japanese text round trip");
    roundTrip("\x1b\x24\x42\x46\x7c\x4b\x5c\x1b\x28\x42",
              "ISO-2022-JP", "ISO-2022-JP stateful round trip");
    roundTrip("\xd6\xd0\xce\xc4", "GBK", "GBK Chinese round trip");
    roundTrip("\xd6\xd0\xce\xc4\x95\x30\x85\x32", "GB18030",
              "GB18030 four-byte Unicode round trip");
    roundTrip("\xc1\x63\xc5\xe9", "Big5", "Big5 Chinese round trip");
    roundTrip("\xc7\xd1\xb1\xdb", "EUC-KR", "EUC-KR Korean round trip");
    roundTrip("\xf0\xd2\xc9\xd7\xc5\xd4", "KOI8-R",
              "KOI8-R Cyrillic round trip");

    stage = "Invalid names";
    bool rejected = false;
    try { (void)ConvertEncoding("x", "NOT-AN-ENCODING"); }
    catch (const std::exception &) { rejected = true; }
    expect(rejected, "Unsupported encoding must fail, never silently relabel");
    std::cout << "Apple CoreFoundation text conversions passed without iconv\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Apple encoding regression failed at " << stage
              << ": " << e.what() << '\n';
    return 1;
  }
}
