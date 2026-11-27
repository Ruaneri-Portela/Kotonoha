// Standalone C++17 test: does not require SDL, proprietary assets or a GPU.
#include <Kotonoha/utils/TextWrapping.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Kotonoha::TextWrapping;

void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

int main() {
  try {
    auto measure = [](const std::string &value) {
      return static_cast<float>(value.size());
    };
    const std::vector<std::string> lines{"first", "second"};
    check(wrap(R"(first\nsecond)", measure, 100) == lines &&
              wrap(R"(first\r\nsecond)", measure, 100) == lines &&
              wrap("first\r\nsecond", measure, 100) == lines &&
              wrap("first\nsecond", measure, 100) == lines,
          "Real and escaped line separators have identical layout");
    check(wrap(R"(a\\nb)", measure, 100) ==
                  std::vector<std::string>{R"(a\nb)"} &&
              wrap(R"(a\qb)", measure, 100) ==
                  std::vector<std::string>{R"(a\qb)"} &&
              wrap(R"(a\)", measure, 100) == std::vector<std::string>{R"(a\)"},
          "Literal backslash, unknown escape and trailing slash survive");
    check(wrap(R"(a\n\nb)", measure, 100) ==
              std::vector<std::string>({"a", "", "b"}),
          "Consecutive newlines retain empty lines");

    // Explicit bytes keep this check independent of the MSVC source code page.
    const std::string utf8 = "\xC3\xA0\xF0\x9F\x8C\xB8";
    check(visibleLines(layout(utf8, measure, 100), 1).empty() &&
              visibleLines(layout(utf8, measure, 100), 2) ==
                  std::vector<std::string>{"\xC3\xA0"},
          "Incomplete UTF-8 is never rendered");
    const std::string invalid("\xc3x", 2);
    check(nextUtf8(invalid, 0) == 1 &&
              wrap(invalid, measure, 100) == std::vector<std::string>{invalid},
          "Malformed UTF-8 remains bounded and is not discarded");

    // Every UTF-8 boundary must be safe, including the end of an empty
    // string and malformed but continuation-shaped byte sequences.
    check(nextUtf8("", 0) == 0 && nextUtf8("a", 1) == 1 &&
              nextUtf8("a", 99) == 1,
          "UTF-8 cursor must stay within its source bounds");
    const auto sequence = [](std::initializer_list<unsigned char> bytes) {
      std::string s;
      for (unsigned char byte : bytes)
        s.push_back(static_cast<char>(byte));
      return s;
    };
    const auto valid3 = sequence({0xe0, 0xa0, 0x80});
    const auto valid4 = sequence({0xf4, 0x8f, 0xbf, 0xbf});
    check(nextUtf8(valid3, 0) == 3 && nextUtf8(valid4, 0) == 4,
          "Legal UTF-8 boundary scalars must stay intact");
    for (const auto &bad : {
             sequence({0xe0, 0x80, 0x80}),       // overlong 3-byte
             sequence({0xed, 0xa0, 0x80}),       // UTF-16 high surrogate
             sequence({0xed, 0xbf, 0xbf}),       // UTF-16 low surrogate
             sequence({0xf0, 0x80, 0x80, 0x80}), // overlong 4-byte
             sequence({0xf4, 0x90, 0x80, 0x80}), // above U+10FFFF
             sequence({0xf5, 0x80, 0x80, 0x80}), // invalid lead
             sequence({0xc0, 0xaf}),             // overlong 2-byte
             sequence({0xe2, 0x82}),             // truncated sequence
             sequence({0x80}),                   // standalone continuation
         }) {
      check(nextUtf8(bad, 0) == 1 &&
                wrap(bad, measure, 100) == std::vector<std::string>{bad},
            "Invalid UTF-8 must not be grouped or silently discarded");
      const auto state = layout(bad, measure, 100);
      for (std::size_t i = 0; i <= bad.size(); ++i) {
        const auto visible = visibleLines(state, i);
        check(i == 0 ? visible.empty()
                     : visible == std::vector<std::string>{bad.substr(0, i)},
              "Malformed UTF-8 must reveal one source byte at a time");
      }
    }

    // Exhaustively verify monotonic reveal and final layout equivalence for
    // short source strings containing escapes, CR, LF and spaces.
    const std::string alphabet = "a b\\nr\n\rt";
    std::size_t cases = 0;
    for (int length = 0; length <= 5; ++length) {
      std::size_t combinations = 1;
      for (int n = 0; n < length; ++n)
        combinations *= alphabet.size();
      for (std::size_t index = 0; index < combinations; ++index) {
        std::string input;
        auto digits = index;
        for (int n = 0; n < length; ++n) {
          input += alphabet[digits % alphabet.size()];
          digits /= alphabet.size();
        }
        const auto wrapped = layout(input, measure, 3.0f);
        check(visibleLines(wrapped, input.size()) == allLines(wrapped),
              "Fully revealed output equals wrapped output");
        std::vector<std::string> previous;
        for (std::size_t bytes = 0; bytes <= input.size(); ++bytes) {
          const auto visible = visibleLines(wrapped, bytes);
          check(visible.size() >= previous.size(),
                "Revealing additional bytes never removes a line");
          for (std::size_t line = 0; line < previous.size(); ++line)
            check(visible[line].compare(0, previous[line].size(),
                                        previous[line]) == 0,
                  "Revealing more bytes never replaces earlier text");
          previous = visible;
        }
        ++cases;
      }
    }
    std::cout << "TextWrapping: " << cases << " cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
