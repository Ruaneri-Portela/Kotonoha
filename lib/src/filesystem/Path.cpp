#include <Kotonoha/Filesystem/IMount.hpp>
#include <Kotonoha/Filesystem/Path.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Kotonoha::Filesystem {
namespace {

bool decodeUtf8(std::string_view input, std::size_t &index,
                std::uint32_t &codepoint) {
  const auto b0 = static_cast<unsigned char>(input[index]);
  if (b0 < 0x80u) {
    codepoint = b0;
    ++index;
    return true;
  }
  if ((b0 & 0xE0u) == 0xC0u && index + 1 < input.size()) {
    const auto b1 = static_cast<unsigned char>(input[index + 1]);
    if ((b1 & 0xC0u) == 0x80u) {
      const std::uint32_t cp =
          (static_cast<std::uint32_t>(b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
      if (cp >= 0x80u) {
        codepoint = cp;
        index += 2;
        return true;
      }
    }
  } else if ((b0 & 0xF0u) == 0xE0u && index + 2 < input.size()) {
    const auto b1 = static_cast<unsigned char>(input[index + 1]);
    const auto b2 = static_cast<unsigned char>(input[index + 2]);
    if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
      const std::uint32_t cp = (static_cast<std::uint32_t>(b0 & 0x0Fu) << 12) |
                               (static_cast<std::uint32_t>(b1 & 0x3Fu) << 6) |
                               (b2 & 0x3Fu);
      if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
        codepoint = cp;
        index += 3;
        return true;
      }
    }
  } else if ((b0 & 0xF8u) == 0xF0u && index + 3 < input.size()) {
    const auto b1 = static_cast<unsigned char>(input[index + 1]);
    const auto b2 = static_cast<unsigned char>(input[index + 2]);
    const auto b3 = static_cast<unsigned char>(input[index + 3]);
    if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u &&
        (b3 & 0xC0u) == 0x80u) {
      const std::uint32_t cp = (static_cast<std::uint32_t>(b0 & 0x07u) << 18) |
                               (static_cast<std::uint32_t>(b1 & 0x3Fu) << 12) |
                               (static_cast<std::uint32_t>(b2 & 0x3Fu) << 6) |
                               (b3 & 0x3Fu);
      if (cp >= 0x10000u && cp <= 0x10FFFFu) {
        codepoint = cp;
        index += 4;
        return true;
      }
    }
  }
  return false;
}

void appendUtf8(std::string &out, std::uint32_t cp) {
  if (cp < 0x80u) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800u) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp < 0x10000u) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

std::uint32_t composePair(std::uint32_t base, std::uint32_t mark) {
  // Japanese Kana dakuten (U+3099) & handakuten (U+309A)
  if (mark == 0x3099u) {
    switch (base) {
    case 0x304Bu:
    case 0x304Du:
    case 0x304Fu:
    case 0x3051u:
    case 0x3053u:
    case 0x3055u:
    case 0x3057u:
    case 0x3059u:
    case 0x305Bu:
    case 0x305Du:
    case 0x305Fu:
    case 0x3061u:
    case 0x3064u:
    case 0x3066u:
    case 0x3068u:
    case 0x306Fu:
    case 0x3072u:
    case 0x3075u:
    case 0x3078u:
    case 0x307Bu:
    case 0x309Du:
    case 0x30ABu:
    case 0x30ADu:
    case 0x30AFu:
    case 0x30B1u:
    case 0x30B3u:
    case 0x30B5u:
    case 0x30B7u:
    case 0x30B9u:
    case 0x30BBu:
    case 0x30BDu:
    case 0x30BFu:
    case 0x30C1u:
    case 0x30C4u:
    case 0x30C6u:
    case 0x30C8u:
    case 0x30CFu:
    case 0x30D2u:
    case 0x30D5u:
    case 0x30D8u:
    case 0x30DBu:
    case 0x30FDu:
      return base + 1u;
    case 0x3046u:
      return 0x3094u;
    case 0x30A6u:
      return 0x30F4u;
    case 0x30EFu:
      return 0x30F7u;
    case 0x30F0u:
      return 0x30F8u;
    case 0x30F1u:
      return 0x30F9u;
    case 0x30F2u:
      return 0x30FAu;
    default:
      return 0;
    }
  }
  if (mark == 0x309Au) {
    switch (base) {
    case 0x306Fu:
    case 0x3072u:
    case 0x3075u:
    case 0x3078u:
    case 0x307Bu:
    case 0x30CFu:
    case 0x30D2u:
    case 0x30D5u:
    case 0x30D8u:
    case 0x30DBu:
      return base + 2u;
    default:
      return 0;
    }
  }
  // Common Latin combining diacritics (Portuguese/Spanish/French/German/Nordic)
  if (mark == 0x0300u) { // grave
    switch (base) {
    case 'A':
      return 0x00C0u;
    case 'E':
      return 0x00C8u;
    case 'I':
      return 0x00CCu;
    case 'O':
      return 0x00D2u;
    case 'U':
      return 0x00D9u;
    case 'a':
      return 0x00E0u;
    case 'e':
      return 0x00E8u;
    case 'i':
      return 0x00ECu;
    case 'o':
      return 0x00F2u;
    case 'u':
      return 0x00F9u;
    default:
      return 0;
    }
  }
  if (mark == 0x0301u) { // acute
    switch (base) {
    case 'A':
      return 0x00C1u;
    case 'E':
      return 0x00C9u;
    case 'I':
      return 0x00CDu;
    case 'O':
      return 0x00D3u;
    case 'U':
      return 0x00DAu;
    case 'Y':
      return 0x00DDu;
    case 'a':
      return 0x00E1u;
    case 'e':
      return 0x00E9u;
    case 'i':
      return 0x00EDu;
    case 'o':
      return 0x00F3u;
    case 'u':
      return 0x00FAu;
    case 'y':
      return 0x00FDu;
    default:
      return 0;
    }
  }
  if (mark == 0x0302u) { // circumflex
    switch (base) {
    case 'A':
      return 0x00C2u;
    case 'E':
      return 0x00CAu;
    case 'I':
      return 0x00CEu;
    case 'O':
      return 0x00D4u;
    case 'U':
      return 0x00DBu;
    case 'a':
      return 0x00E2u;
    case 'e':
      return 0x00EAu;
    case 'i':
      return 0x00EEu;
    case 'o':
      return 0x00F4u;
    case 'u':
      return 0x00FBu;
    default:
      return 0;
    }
  }
  if (mark == 0x0303u) { // tilde
    switch (base) {
    case 'A':
      return 0x00C3u;
    case 'N':
      return 0x00D1u;
    case 'O':
      return 0x00D5u;
    case 'a':
      return 0x00E3u;
    case 'n':
      return 0x00F1u;
    case 'o':
      return 0x00F5u;
    default:
      return 0;
    }
  }
  if (mark == 0x0308u) { // diaeresis
    switch (base) {
    case 'A':
      return 0x00C4u;
    case 'E':
      return 0x00CBu;
    case 'I':
      return 0x00CFu;
    case 'O':
      return 0x00D6u;
    case 'U':
      return 0x00DCu;
    case 'Y':
      return 0x0178u;
    case 'a':
      return 0x00E4u;
    case 'e':
      return 0x00EBu;
    case 'i':
      return 0x00EFu;
    case 'o':
      return 0x00F6u;
    case 'u':
      return 0x00FCu;
    case 'y':
      return 0x00FFu;
    default:
      return 0;
    }
  }
  if (mark == 0x030Au) { // ring above
    if (base == 'A')
      return 0x00C5u;
    if (base == 'a')
      return 0x00E5u;
    return 0;
  }
  if (mark == 0x0327u) { // cedilla
    if (base == 'C')
      return 0x00C7u;
    if (base == 'c')
      return 0x00E7u;
    return 0;
  }
  // Hangul L + V -> LV syllable
  if (base >= 0x1100u && base <= 0x1112u && mark >= 0x1161u &&
      mark <= 0x1175u) {
    return 0xAC00u + (base - 0x1100u) * 588u + (mark - 0x1161u) * 28u;
  }
  // Hangul LV + T -> LVT syllable
  if (base >= 0xAC00u && base <= 0xD7A3u && ((base - 0xAC00u) % 28u) == 0u &&
      mark >= 0x11A8u && mark <= 0x11C2u) {
    return base + (mark - 0x11A7u);
  }
  return 0;
}

} // namespace

std::string normalizeUtf8Nfc(std::string_view input) {
  bool hasNonAscii = false;
  for (unsigned char ch : input) {
    if (ch >= 0x80u) {
      hasNonAscii = true;
      break;
    }
  }
  if (!hasNonAscii)
    return std::string(input);

  std::string out;
  out.reserve(input.size());
  bool hasPending = false;
  std::uint32_t pending = 0;

  std::size_t i = 0;
  while (i < input.size()) {
    std::uint32_t cp = 0;
    if (!decodeUtf8(input, i, cp)) {
      if (hasPending) {
        appendUtf8(out, pending);
        hasPending = false;
      }
      out.push_back(input[i++]);
      continue;
    }
    if (hasPending) {
      if (const auto composed = composePair(pending, cp); composed != 0) {
        pending = composed;
        continue;
      }
      appendUtf8(out, pending);
    }
    pending = cp;
    hasPending = true;
  }
  if (hasPending)
    appendUtf8(out, pending);
  return out;
}

std::string normalizePath(std::string_view value, bool allowRoot) {
  if (value.empty() && allowRoot)
    return {};
  if (value.empty())
    throw FilesystemError(ErrorCode::InvalidPath, "Empty virtual path");

  std::string source(value);
  if (source.find('\0') != std::string::npos ||
      source.find(':') != std::string::npos)
    throw FilesystemError(ErrorCode::InvalidPath, "Invalid virtual path");

  std::replace(source.begin(), source.end(), '\\', '/');
  if (source.front() == '/')
    throw FilesystemError(ErrorCode::InvalidPath,
                          "Virtual path cannot be absolute");

  std::vector<std::string> parts;
  std::size_t from = 0;
  while (from < source.size()) {
    const auto end = source.find('/', from);
    const auto size = (end == std::string::npos ? source.size() : end) - from;
    const auto part = source.substr(from, size);
    if (part == "..")
      throw FilesystemError(ErrorCode::InvalidPath,
                            "Traversal in virtual path");
    if (!part.empty() && part != ".")
      parts.push_back(part);
    if (end == std::string::npos)
      break;
    from = end + 1;
  }
  if (parts.empty()) {
    if (allowRoot)
      return {};
    throw FilesystemError(ErrorCode::InvalidPath, "Invalid virtual path");
  }
  std::string out;
  for (const auto &part : parts) {
    if (!out.empty())
      out.push_back('/');
    out += part;
  }
  return out;
}

std::string lookupKey(std::string_view path, bool allowRoot) {
  auto key = normalizeUtf8Nfc(normalizePath(path, allowRoot));
  for (char &ch : key)
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch - 'A' + 'a');
  return key;
}

} // namespace Kotonoha::Filesystem
