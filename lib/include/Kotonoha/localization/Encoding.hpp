#pragma once

#include <string>
#include <string_view>

namespace Kotonoha {

// Converts text between encodings using iconv; invalid input sequences become
// '?'. Throws std::runtime_error when the conversion is unavailable.
std::string ConvertEncoding(std::string_view text, const char *from,
                            const char *to = "UTF-8");

// Shift-JIS (Windows CP932, including the NEC/IBM extensions used by Japanese
// games) to UTF-8.
std::string ShiftJisToUtf8(std::string_view text);

} // namespace Kotonoha
