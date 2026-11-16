// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace RealLive {
// The original RealLive scripts operate on Shift-JIS encoded byte strings.
// A lead byte consumes its trail when present, including non-ASCII glyphs.
// Embedded NUL is legitimate game state and is never an implicit terminator.
inline std::size_t nextScriptCharacter(std::string_view s, std::size_t offset) {
  if (offset >= s.size()) return s.size();
  const auto ch = static_cast<unsigned char>(s[offset++]);
  if (((ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc)) &&
      offset < s.size())
    ++offset;
  return offset;
}
inline std::size_t scriptCharOffset(std::string_view s, std::size_t count) {
  std::size_t offset = 0;
  for (std::size_t n = 0; n < count && offset < s.size(); ++n)
    offset = nextScriptCharacter(s, offset);
  return offset;
}
inline std::size_t scriptCharCount(std::string_view s) {
  std::size_t count = 0, offset = 0;
  while (offset < s.size()) {
    offset = nextScriptCharacter(s, offset);
    ++count;
  }
  return count;
}
inline std::string scriptSubstring(std::string_view source,
                                   int start, std::optional<int> count = {}) {
  if (start < 0)
    throw std::out_of_range("REALLIVE negative substring offset");
  const auto first = scriptCharOffset(source, static_cast<std::size_t>(start));
  if (!count)
    return std::string(source.substr(first));
  const auto length = std::max(0, *count);
  // Count from the first character's byte offset; adding two signed int
  // parameters before clamping can overflow in retail script data.
  const auto bytes = scriptCharOffset(source.substr(first),
                                      static_cast<std::size_t>(length));
  return std::string(source.substr(first, bytes));
}
inline int scriptStringCompare(std::string_view a, std::string_view b) {
  // std::strcmp stops at the first embedded NUL and can compare distinct
  // script strings as identical. The VM uses length-preserving string state.
  return a.compare(b);
}
} // namespace RealLive
