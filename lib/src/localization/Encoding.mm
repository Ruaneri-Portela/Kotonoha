// Apple implementation of the shared text codec. No SDL_iconv or libiconv.
// CoreFoundation provides the Unicode and legacy encodings required by the
// engine, including the Windows Japanese CP932 extensions.
#include <Kotonoha/localization/Encoding.hpp>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFStringEncodingExt.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Kotonoha {
namespace {

class OwnedString {
public:
  explicit OwnedString(CFStringRef value) : value_(value) {}
  ~OwnedString() {
    if (value_)
      CFRelease(value_);
  }
  OwnedString(const OwnedString &) = delete;
  OwnedString &operator=(const OwnedString &) = delete;
  CFStringRef get() const { return value_; }

private:
  CFStringRef value_;
};

std::string normalizedName(const char *name) {
  if (!name || !*name)
    throw std::invalid_argument("Text encoding name must not be empty");
  std::string result;
  for (const unsigned char *p =
           reinterpret_cast<const unsigned char *>(name); *p; ++p) {
    if (std::isalnum(*p))
      result.push_back(char(std::toupper(*p)));
    else if (*p != '-' && *p != '_' && *p != ' ' && *p != '.')
      throw std::invalid_argument("Invalid text encoding name");
  }
  if (result.empty())
    throw std::invalid_argument("Invalid text encoding name");
  return result;
}

CFStringEncoding resolveEncoding(const char *name) {
  const std::string key = normalizedName(name);
  if (key == "UTF8") return kCFStringEncodingUTF8;
  if (key == "UTF16") return kCFStringEncodingUTF16;
  if (key == "UTF16LE") return kCFStringEncodingUTF16LE;
  if (key == "UTF16BE") return kCFStringEncodingUTF16BE;
  if (key == "UTF32") return kCFStringEncodingUTF32;
  if (key == "UTF32LE") return kCFStringEncodingUTF32LE;
  if (key == "UTF32BE") return kCFStringEncodingUTF32BE;
  if (key == "ASCII" || key == "USASCII") return kCFStringEncodingASCII;
  if (key == "LATIN1" || key == "ISO88591" || key == "ISOIR100")
    return kCFStringEncodingISOLatin1;
  if (key == "CP1252" || key == "WINDOWS1252" || key == "MSANSI")
    return kCFStringEncodingWindowsLatin1;
  // These aliases intentionally mean Microsoft's Windows-31J, not strict
  // JIS Shift-JIS: game scripts rely on NEC/IBM symbols and U+FF5E.
  if (key == "CP932" || key == "WINDOWS31J" || key == "WINDOWS932" ||
      key == "SJIS" || key == "SHIFTJIS" || key == "MSKANJI" ||
      key == "MS932")
    return kCFStringEncodingDOSJapanese;
  if (key == "EUCJP") return kCFStringEncodingEUC_JP;
  if (key == "ISO2022JP") return kCFStringEncodingISO_2022_JP;
  if (key == "GB18030") return kCFStringEncodingGB_18030_2000;
  if (key == "GBK" || key == "CP936" || key == "WINDOWS936")
    return kCFStringEncodingDOSChineseSimplif;
  if (key == "BIG5" || key == "CP950") return kCFStringEncodingBig5;
  if (key == "EUCKR") return kCFStringEncodingEUC_KR;
  if (key == "CP949" || key == "WINDOWS949")
    return kCFStringEncodingDOSKorean;
  if (key == "KOI8R") return kCFStringEncodingKOI8_R;

  // Allow further IANA encodings supported by the installed Apple framework.
  OwnedString iana(CFStringCreateWithCString(kCFAllocatorDefault, name,
                                             kCFStringEncodingASCII));
  if (!iana.get())
    throw std::invalid_argument("Invalid IANA text encoding name");
  const auto encoding = CFStringConvertIANACharSetNameToEncoding(iana.get());
  if (encoding == kCFStringEncodingInvalidId)
    throw std::runtime_error(std::string("Unsupported text encoding: ") + name);
  return encoding;
}

CFIndex checkedLength(size_t size) {
  if (size > size_t(std::numeric_limits<CFIndex>::max() / 8))
    throw std::length_error("Text is too large to convert");
  return CFIndex(size);
}

CFStringRef createString(std::string_view bytes, CFStringEncoding encoding) {
  if (bytes.empty())
    return CFStringCreateWithCharacters(kCFAllocatorDefault, nullptr, 0);
  return CFStringCreateWithBytes(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(bytes.data()),
      checkedLength(bytes.size()), encoding, false);
}

CFMutableStringRef repairedSupplementary(CFStringRef value,
                                           CFStringEncoding encoding) {
  // CFStringGetBytes can emit TWO loss bytes for an unrepresentable Unicode
  // surrogate pair. Legacy codepages need exactly one '?' per scalar.
  CFMutableStringRef repaired = nullptr;
  for (CFIndex i = CFStringGetLength(value); i > 0;) {
    --i;
    const UniChar low = CFStringGetCharacterAtIndex(value, i);
    if (low < 0xdc00 || low > 0xdfff || !i)
      continue;
    const UniChar high = CFStringGetCharacterAtIndex(value, i - 1);
    if (high < 0xd800 || high > 0xdbff)
      continue;
    UInt8 buffer[64]{};
    CFIndex used = 0;
    const CFIndex consumed = CFStringGetBytes(
        value, CFRangeMake(i - 1, 2), encoding, 0, false, buffer,
        CFIndex(sizeof buffer), &used);
    if (consumed != 2) {
      if (!repaired)
        repaired = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, value);
      if (!repaired)
        throw std::runtime_error("Cannot allocate Apple text fallback");
      CFStringReplace(repaired, CFRangeMake(i - 1, 2), CFSTR("?"));
    }
    --i;
  }
  return repaired;
}

std::string encodeString(CFStringRef value, CFStringEncoding encoding) {
  OwnedString repaired(repairedSupplementary(value, encoding));
  if (repaired.get())
    value = repaired.get();
  const CFIndex count = CFStringGetLength(value);
  if (!count)
    return {};
  // Space for 32-bit Unicode and for stateful legacy output escapes.
  size_t capacity = size_t(count) * 8 + 16;
  for (int attempt = 0; attempt < 12; ++attempt) {
    std::string result(capacity, '\0');
    CFIndex used = 0;
    const CFIndex converted = CFStringGetBytes(
        value, CFRangeMake(0, count), encoding, UInt8('?'), false,
        reinterpret_cast<UInt8 *>(result.data()), checkedLength(capacity),
        &used);
    if (converted == count) {
      result.resize(size_t(used));
      return result;
    }
    if (used < CFIndex(capacity))
      break; // The conversion failed, not the output buffer.
    if (capacity > size_t(std::numeric_limits<CFIndex>::max() / 16))
      break;
    capacity *= 2;
  }
  throw std::runtime_error("CoreFoundation text encoding conversion failed");
}

bool isLead932(unsigned char b) {
  return (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc);
}
bool isTrail932(unsigned char b) {
  return b >= 0x40 && b <= 0xfc && b != 0x7f;
}
uint16_t read16(std::string_view data, size_t pos, bool be) {
  const uint8_t x = uint8_t(data[pos]), y = uint8_t(data[pos + 1]);
  return be ? uint16_t((x << 8) | y) : uint16_t((y << 8) | x);
}

// An invalid sequence becomes one '?' without discarding a following valid
// ASCII byte. An unmapped but structurally valid two-byte CP932 pair consumes
// both bytes, matching the existing Windows and SDL fallback contract.
size_t nextUnit(std::string_view bytes, size_t pos,
                const std::string &key) {
  const size_t remaining = bytes.size() - pos;
  const auto first = uint8_t(bytes[pos]);
  if (key == "UTF16LE" || key == "UTF16BE" || key == "UTF16") {
    if (remaining < 2) return remaining;
    const bool be = key == "UTF16BE";
    const uint16_t lead = read16(bytes, pos, be);
    if (lead >= 0xd800 && lead <= 0xdbff && remaining >= 4) {
      const auto trail = read16(bytes, pos + 2, be);
      if (trail >= 0xdc00 && trail <= 0xdfff) return 4;
    }
    return 2;
  }
  if (key == "UTF32LE" || key == "UTF32BE" || key == "UTF32")
    return std::min<size_t>(4, remaining);
  if (key == "UTF8") {
    size_t width = first < 0x80 ? 1 : (first >= 0xc2 && first <= 0xdf) ? 2
                   : (first >= 0xe0 && first <= 0xef) ? 3
                   : (first >= 0xf0 && first <= 0xf4) ? 4 : 1;
    if (width > remaining) return 1;
    for (size_t i = 1; i < width; ++i)
      if ((uint8_t(bytes[pos + i]) & 0xc0) != 0x80) return 1;
    return width;
  }
  if (key == "CP932" || key == "WINDOWS31J" || key == "WINDOWS932" ||
      key == "SJIS" || key == "SHIFTJIS" || key == "MSKANJI" ||
      key == "MS932") {
    if (isLead932(first) && remaining > 1 &&
        isTrail932(uint8_t(bytes[pos + 1]))) return 2;
    return 1;
  }
  if (key == "EUCJP") {
    if (first == 0x8f && remaining >= 3) return 3;
    if ((first == 0x8e || first >= 0xa1) && remaining >= 2) return 2;
  }
  if (key == "GB18030" || key == "GBK" || key == "CP936" ||
      key == "WINDOWS936") {
    if (first >= 0x81 && first <= 0xfe && remaining >= 2) {
      const auto second = uint8_t(bytes[pos + 1]);
      if (key == "GB18030" && second >= 0x30 && second <= 0x39 &&
          remaining >= 4) return 4;
      return 2;
    }
  }
  if (key == "BIG5" || key == "CP950" || key == "EUCKR" ||
      key == "CP949" || key == "WINDOWS949")
    if (first >= 0x81 && remaining >= 2) return 2;
  return 1;
}

void replaceAll(std::string &text, const std::string &from,
                const std::string &to) {
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

bool isWindowsJapanese(const std::string &key) {
  return key == "CP932" || key == "WINDOWS31J" || key == "WINDOWS932" ||
         key == "SJIS" || key == "SHIFTJIS" || key == "MSKANJI" ||
         key == "MS932";
}
void normalizeCp932Unicode(std::string &utf8) {
  // Some Apple tables use JIS Unicode punctuation code points instead of
  // the Windows mappings. Normalize these six historical compatibility pairs
  // for deterministic CP932 script text across native platforms.
  replaceAll(utf8, "\xE3\x80\x9C", "\xEF\xBD\x9E"); // wave dash -> fullwidth tilde
  replaceAll(utf8, "\xE2\x80\x96", "\xE2\x88\xA5"); // double vertical -> parallel
  replaceAll(utf8, "\xE2\x88\x92", "\xEF\xBC\x8D"); // minus -> fullwidth hyphen
  replaceAll(utf8, "\xC2\xA2", "\xEF\xBF\xA0");     // cent -> fullwidth cent
  replaceAll(utf8, "\xC2\xA3", "\xEF\xBF\xA1");     // pound -> fullwidth pound
  replaceAll(utf8, "\xC2\xAC", "\xEF\xBF\xA2");     // not -> fullwidth not
}
} // namespace


void appendUtf8Scalar(std::string &out, uint32_t value) {
  if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
    value = '?';
  if (value <= 0x7f) {
    out.push_back(char(value));
  } else if (value <= 0x7ff) {
    out.push_back(char(0xc0 | (value >> 6)));
    out.push_back(char(0x80 | (value & 0x3f)));
  } else if (value <= 0xffff) {
    out.push_back(char(0xe0 | (value >> 12)));
    out.push_back(char(0x80 | ((value >> 6) & 0x3f)));
    out.push_back(char(0x80 | (value & 0x3f)));
  } else {
    out.push_back(char(0xf0 | (value >> 18)));
    out.push_back(char(0x80 | ((value >> 12) & 0x3f)));
    out.push_back(char(0x80 | ((value >> 6) & 0x3f)));
    out.push_back(char(0x80 | (value & 0x3f)));
  }
}

std::string decodeUtf16(std::string_view bytes, bool bigEndian) {
  std::string out;
  out.reserve(bytes.size());
  for (size_t i = 0; i < bytes.size();) {
    if (bytes.size() - i < 2) {
      out.push_back('?');
      break;
    }
    uint32_t scalar = read16(bytes, i, bigEndian);
    i += 2;
    if (scalar >= 0xd800 && scalar <= 0xdbff) {
      if (bytes.size() - i >= 2) {
        const uint32_t low = read16(bytes, i, bigEndian);
        if (low >= 0xdc00 && low <= 0xdfff) {
          scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low - 0xdc00);
          i += 2;
        } else {
          scalar = '?';
        }
      } else {
        scalar = '?';
      }
    } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
      scalar = '?';
    }
    appendUtf8Scalar(out, scalar);
  }
  return out;
}

std::string decodeUtf32(std::string_view bytes, bool bigEndian) {
  std::string out;
  out.reserve(bytes.size());
  for (size_t i = 0; i < bytes.size();) {
    if (bytes.size() - i < 4) {
      out.push_back('?'); // Incomplete trailing codepoint.
      break;
    }
    uint32_t scalar = 0;
    for (int b = 0; b < 4; ++b) {
      const uint32_t octet = uint8_t(bytes[i + b]);
      scalar = bigEndian ? ((scalar << 8) | octet)
                         : (scalar | (octet << (8 * b)));
    }
    appendUtf8Scalar(out, scalar);
    i += 4;
  }
  return out;
}

std::string encodeUtf32(std::string_view utf8, bool bigEndian) {
  OwnedString unicode(createString(utf8, kCFStringEncodingUTF8));
  if (!unicode.get())
    throw std::runtime_error("UTF-32 output requires valid UTF-8");
  const CFIndex length = CFStringGetLength(unicode.get());
  std::string output;
  output.reserve(size_t(length) * 4);
  for (CFIndex i = 0; i < length; ++i) {
    uint32_t scalar = CFStringGetCharacterAtIndex(unicode.get(), i);
    if (scalar >= 0xd800 && scalar <= 0xdbff && i + 1 < length) {
      const uint32_t low = CFStringGetCharacterAtIndex(unicode.get(), i + 1);
      if (low >= 0xdc00 && low <= 0xdfff) {
        scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low - 0xdc00);
        ++i;
      } else {
        scalar = '?';
      }
    } else if (scalar >= 0xd800 && scalar <= 0xdfff) {
      scalar = '?';
    }
    for (int b = 0; b < 4; ++b) {
      const unsigned shift = bigEndian ? unsigned(8 * (3 - b)) : unsigned(8 * b);
      output.push_back(char((scalar >> shift) & 0xff));
    }
  }
  return output;
}

std::string ConvertEncoding(std::string_view text, const char *from,
                            const char *to) {
  const auto sourceEncoding = resolveEncoding(from);
  const auto targetEncoding = resolveEncoding(to);
  if (text.empty())
    return {};
  const auto sourceKey = normalizedName(from);
  const auto targetKey = normalizedName(to);
  // Apple's CFString conversion can fail for explicit UTF-32LE/BE output.
  // Encode/decode the Unicode scalars ourselves, in this same Apple backend,
  // avoiding an iconv dependency and preserving embedded NUL bytes.
  if (sourceKey == "UTF16LE" || sourceKey == "UTF16BE")
    return ConvertEncoding(decodeUtf16(text, sourceKey == "UTF16BE"),
                           "UTF-8", to);
  if (sourceKey == "UTF32LE" || sourceKey == "UTF32BE")
    return ConvertEncoding(decodeUtf32(text, sourceKey == "UTF32BE"),
                           "UTF-8", to);
  if (targetKey == "UTF32LE" || targetKey == "UTF32BE")
    return encodeUtf32(ConvertEncoding(text, from, "UTF-8"),
                       targetKey == "UTF32BE");
  // CP1252's five undefined bytes must be recoverable as '?'.
  std::string cleaned;
  if (sourceEncoding == kCFStringEncodingWindowsLatin1) {
    cleaned.assign(text);
    for (char &c : cleaned)
      if (uint8_t(c) == 0x81 || uint8_t(c) == 0x8d ||
          uint8_t(c) == 0x8f || uint8_t(c) == 0x90 || uint8_t(c) == 0x9d)
        c = '?';
    text = cleaned;
  }
  if (isWindowsJapanese(sourceKey)) {
    // Apple's DOSJapanese decoder accepts several reserved lead-byte rows
    // using private mappings. Windows CP932 (and the existing engine tests)
    // mark these rows as undefined. Reject the structurally valid pair as
    // one replacement instead of emitting a platform-specific glyph.
    std::string filtered;
    filtered.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
      const auto b = uint8_t(text[i]);
      if (isLead932(b)) {
        if (i + 1 >= text.size() || !isTrail932(uint8_t(text[i + 1]))) {
          filtered.push_back('?');
          ++i; // The following ASCII/trail byte remains independently valid.
        } else if (b == 0x85 || b == 0x86 || b == 0xeb || b == 0xec) {
          filtered.push_back('?');
          i += 2;
        } else {
          filtered.append(text.data() + i, 2);
          i += 2;
        }
      } else if (b == 0xa0 || b >= 0xfd) {
        filtered.push_back('?');
        ++i;
      } else {
        filtered.push_back(text[i]);
        ++i;
      }
    }
    cleaned = std::move(filtered);
    text = cleaned;
  }
  OwnedString decoded(createString(text, sourceEncoding));
  if (decoded.get()) {
    if (isWindowsJapanese(sourceKey)) {
      std::string normalized = encodeString(decoded.get(), kCFStringEncodingUTF8);
      normalizeCp932Unicode(normalized);
      OwnedString fixed(createString(normalized, kCFStringEncodingUTF8));
      if (!fixed.get())
        throw std::runtime_error("Invalid CoreFoundation CP932 normalization");
      return encodeString(fixed.get(), targetEncoding);
    }
    return encodeString(decoded.get(), targetEncoding);
  }

  // Recover from malformed bytes while retaining valid neighboring text.
  std::string recovered;
  recovered.reserve(text.size() + 8);
  for (size_t i = 0; i < text.size();) {
    const size_t count = nextUnit(text, i, sourceKey);
    OwnedString one(createString(text.substr(i, count), sourceEncoding));
    if (!one.get()) {
      recovered.push_back('?');
    } else {
      recovered += encodeString(one.get(), kCFStringEncodingUTF8);
    }
    i += count;
  }
  if (isWindowsJapanese(sourceKey))
    normalizeCp932Unicode(recovered);
  OwnedString repaired(createString(recovered, kCFStringEncodingUTF8));
  if (!repaired.get())
    throw std::runtime_error("Failed to repair invalid text input");
  return encodeString(repaired.get(), targetEncoding);
}

std::string ShiftJisToUtf8(std::string_view text) {
  return ConvertEncoding(text, "CP932", "UTF-8");
}

std::u16string Utf8ToUtf16(std::string_view text) {
  if (text.empty()) return {};
  OwnedString unicode(createString(text, kCFStringEncodingUTF8));
  if (!unicode.get()) return {};
  const CFIndex length = CFStringGetLength(unicode.get());
  std::u16string result(size_t(length), char16_t{});
  CFStringGetCharacters(unicode.get(), CFRangeMake(0, length),
                        reinterpret_cast<UniChar *>(result.data()));
  return result;
}

std::string Utf16ToUtf8(std::u16string_view text) {
  if (text.empty()) return {};
  OwnedString unicode(CFStringCreateWithCharacters(
      kCFAllocatorDefault, reinterpret_cast<const UniChar *>(text.data()),
      checkedLength(text.size())));
  if (!unicode.get()) return {};
  try {
    return encodeString(unicode.get(), kCFStringEncodingUTF8);
  } catch (const std::runtime_error &) {
    return {};
  }
}
} // namespace Kotonoha
#endif // __APPLE__
