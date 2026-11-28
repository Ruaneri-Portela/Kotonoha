#pragma once
#include <Kotonoha/localization/Encoding.hpp>

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Kotonoha {

// json-c 0.18's strict tokenizer rejects literal non-ASCII UTF-8 on
// some builds. Feed it equivalent ASCII JSON, without weakening strict
// syntax checking or modifying already escaped string contents.
inline std::string StrictJsonSource(std::string_view source) {
  const auto unicode = Utf8ToUtf16(source);
  if (Utf16ToUtf8(unicode) != source)
    throw std::runtime_error("Invalid UTF-8 in JSON");
  std::string result;
  result.reserve(source.size());
  bool quoted = false;
  bool escaped = false;
  constexpr char hex[] = "0123456789abcdef";
  for (const char16_t ch : unicode) {
    if (ch == 0)
      throw std::runtime_error("Embedded NUL in JSON");
    if (ch < 0x80) {
      result.push_back(static_cast<char>(ch));
      if (escaped) {
        escaped = false;
      } else if (quoted && ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        quoted = !quoted;
      }
    } else {
      if (!quoted || escaped)
        throw std::runtime_error("Invalid non-ASCII JSON syntax");
      result += "\\u";
      for (int shift = 12; shift >= 0; shift -= 4)
        result.push_back(hex[(ch >> shift) & 15]);
    }
    if (result.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
      throw std::runtime_error("JSON is too large");
  }
  return result;
}

} // namespace Kotonoha
