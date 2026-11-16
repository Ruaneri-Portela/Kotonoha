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
              wrap(R"(a\)", measure, 100) ==
                  std::vector<std::string>{R"(a\)"},
          "Literal backslash, unknown escape and trailing slash survive");
    check(wrap(R"(a\n\nb)", measure, 100) ==
              std::vector<std::string>({"a", "", "b"}),
          "Consecutive newlines retain empty lines");

    const std::string utf8 = u8"à🌸";
    check(visibleLines(layout(utf8, measure, 100), 1).empty() &&
              visibleLines(layout(utf8, measure, 100), 2) ==
                  std::vector<std::string>{u8"à"},
          "Incomplete UTF-8 is never rendered");
    const std::string invalid("\xc3x", 2);
    check(nextUtf8(invalid, 0) == 1 && wrap(invalid, measure, 100) ==
                                            std::vector<std::string>{invalid},
          "Malformed UTF-8 remains bounded and is not discarded");

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
