#include <Kotonoha/localization/Encoding.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "EncodingUtfTests FAILED: " << message << '\n';
    std::exit(2);
  }
}

void CheckRoundTrip(const std::string &utf8, const char *label) {
  const std::u16string utf16 = Kotonoha::Utf8ToUtf16(utf8);
  const std::string back = Kotonoha::Utf16ToUtf8(utf16);
  Require(back == utf8, label);
}

} // namespace

int main() {
  Require(Kotonoha::Utf8ToUtf16("").empty(), "empty UTF-8 -> UTF-16");
  Require(Kotonoha::Utf16ToUtf8(u"").empty(), "empty UTF-16 -> UTF-8");

  // Short ASCII punctuation sequence that previously triggered iconv E2BIG
  // loops on Android/POSIX backends.
  const std::u16string dots = Kotonoha::Utf8ToUtf16("......");
  Require(dots == u"......", "ASCII six dots");
  Require(Kotonoha::Utf16ToUtf8(dots) == "......", "ASCII round-trip");

  // Multilingual BMP scripts (Latin accents, Cyrillic, Japanese, Chinese).
  CheckRoundTrip("Ela parecia gostar de livros. N\xC3\xA3o era uma descoberta profunda...",
                 "PT-BR accents round-trip");
  CheckRoundTrip("\xC2\xBFPor qu\xC3\xA9 todav\xC3\xAD""a eres pobre?", "ES accents round-trip");
  CheckRoundTrip("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, \xD0\xBC\xD0\xB8\xD1\x80!",
                 "Russian Cyrillic round-trip");
  CheckRoundTrip("\xE8\xA8\x80\xE8\x91\x89\xE3\x83\xBB\xE4\xB8\x96\xE7\x95\x8C\xE3\x83\xBB\xE8\xAA\xA0",
                 "Japanese/CJK round-trip");
  CheckRoundTrip("\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\xE5\xB9\x95",
                 "Simplified Chinese round-trip");
  // U+1F600 (GRINNING FACE) -> surrogate pair D83D DE00.
  const std::string emoji = "\xF0\x9F\x98\x80";
  const std::u16string emoji16 = Kotonoha::Utf8ToUtf16(emoji);
  Require(emoji16.size() == 2 && emoji16[0] == 0xD83Du && emoji16[1] == 0xDE00u,
          "astral UTF-8 -> UTF-16 surrogate pair");
  Require(Kotonoha::Utf16ToUtf8(emoji16) == emoji, "surrogate pair -> UTF-8");

  // Invalid UTF-8 bytes and lone surrogates must never hang or crash; each
  // becomes U+FFFD.
  const std::string badUtf8 = std::string("A") + "\xFF\xFE" + "B";
  const std::u16string bad16 = Kotonoha::Utf8ToUtf16(badUtf8);
  Require(bad16.size() == 4 && bad16[0] == u'A' && bad16[1] == 0xFFFDu &&
              bad16[2] == 0xFFFDu && bad16[3] == u'B',
          "invalid UTF-8 replaced by U+FFFD");

  const std::u16string loneHigh{u'X', static_cast<char16_t>(0xD800u), u'Y'};
  Require(Kotonoha::Utf16ToUtf8(loneHigh) == "X\xEF\xBF\xBDY",
          "lone high surrogate replaced by U+FFFD");

  const std::u16string loneLow{u'X', static_cast<char16_t>(0xDC00u), u'Y'};
  Require(Kotonoha::Utf16ToUtf8(loneLow) == "X\xEF\xBF\xBDY",
          "lone low surrogate replaced by U+FFFD");

  std::cout << "EncodingUtfTests PASS\n";
  return 0;
}
