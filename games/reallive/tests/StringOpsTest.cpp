// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <RealLive/StringOps.hpp>
#include <climits>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  try {
    const std::string text = {char(0x82), char(0xa0), 'A', '\0', 'Z'};
    require(RealLive::scriptCharCount(text) == 4,
            "Shift-JIS glyphs and embedded NUL count as characters");
    require(RealLive::scriptCharOffset(text, 1) == 2 &&
                RealLive::scriptCharOffset(text, 3) == 4 &&
                RealLive::scriptCharOffset(
                    text, std::numeric_limits<std::size_t>::max()) ==
                    text.size(),
            "S-JIS offsets clamp without integer overflow");
    // Str.strcpy's count overload preserves complete Shift-JIS characters.
    auto copied = text;
    copied.resize(RealLive::scriptCharOffset(copied, 1));
    require(copied == std::string(text.data(), 2),
            "strcpy count must not split Shift-JIS characters");
    copied = text;
    copied.resize(RealLive::scriptCharOffset(copied, 2));
    require(copied == std::string(text.data(), 3),
            "strcpy count includes subsequent ASCII characters");
    copied.resize(RealLive::scriptCharOffset(copied, 0));
    require(copied.empty(), "zero-length strcpy is empty");
    require(RealLive::scriptSubstring(text, 0, 1) ==
                    std::string(text.data(), 2) &&
                RealLive::scriptSubstring(text, 1, 2) ==
                    std::string(text.data() + 2, 2),
            "substrings preserve S-JIS glyph boundaries and NUL bytes");
    require(RealLive::scriptSubstring(text, INT_MAX, INT_MAX).empty(),
            "large substring offsets do not overflow signed integers");
    require(RealLive::scriptSubstring(text, 2) ==
                std::string(text.data() + 3, 2),
            "no-length substring copies the remaining bytes");
    bool caught = false;
    try {
      (void)RealLive::scriptSubstring(text, -1);
    } catch (const std::out_of_range &) {
      caught = true;
    }
    require(caught, "negative offsets must be rejected");

    const std::string a("item\0a", 6), b("item\0b", 6);
    require(RealLive::scriptStringCompare(a, b) < 0 &&
                RealLive::scriptStringCompare(a, a) == 0 &&
                RealLive::scriptStringCompare(b, a) > 0,
            "string compare must inspect bytes after embedded NUL");
    const std::string truncated = {char(0x82)};
    require(RealLive::scriptCharCount(truncated) == 1 &&
                RealLive::scriptCharOffset(truncated, 1) == 1,
            "truncated S-JIS lead byte remains bounded");
    std::cout << "RealLive string operation tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "RealLive string test failed: " << e.what() << '\n';
    return 1;
  }
}
