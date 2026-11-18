#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Kotonoha::TextWrapping {

// Each displayed character remembers its exact byte range in the source.
// Escape expansion and typewriter reveal never split an escape or UTF-8 glyph.
struct Unit {
  std::string display;
  std::size_t start = 0;
  std::size_t end = 0;
  enum class Kind { Word, Space, Cjk, Newline } kind = Kind::Word;
};

struct Line {
  std::string text;
  std::vector<Unit> units;
  std::size_t start = 0;
  std::size_t newlineEnd = 0;
};

using Layout = std::vector<Line>;

inline bool continuation(unsigned char ch) { return (ch & 0xc0u) == 0x80u; }

// Advance over one well-formed UTF-8 scalar value. Invalid, truncated,
// overlong and surrogate sequences advance by a single byte; callers can
// preserve the original data without combining malformed bytes into a glyph.
inline std::size_t nextUtf8(const std::string &source, std::size_t start) {
  if (start >= source.size())
    return source.size();
  const unsigned char lead = static_cast<unsigned char>(source[start]);
  const std::size_t length = lead < 0x80u                       ? 1
                             : lead >= 0xc2u && lead <= 0xdfu ? 2
                             : lead >= 0xe0u && lead <= 0xefu ? 3
                             : lead >= 0xf0u && lead <= 0xf4u ? 4
                                                               : 1;
  if (length == 1 || length > source.size() - start)
    return start + 1;
  for (std::size_t i = 1; i < length; ++i)
    if (!continuation(static_cast<unsigned char>(source[start + i])))
      return start + 1;

  // Continuation bits alone are insufficient: reject overlong encodings,
  // UTF-16 surrogate code points, and values beyond U+10FFFF.
  const unsigned char second =
      static_cast<unsigned char>(source[start + 1]);
  if ((lead == 0xe0u && second < 0xa0u) ||
      (lead == 0xedu && second >= 0xa0u) ||
      (lead == 0xf0u && second < 0x90u) ||
      (lead == 0xf4u && second > 0x8fu))
    return start + 1;
  return start + length;
}

inline std::vector<Unit> tokenize(const std::string &source) {
  std::vector<Unit> result;
  // Avoid allocating a huge Unit array for multibyte or untrusted text.
  result.reserve(std::min(source.size(), std::size_t{1024}));
  for (std::size_t i = 0; i < source.size();) {
    const std::size_t begin = i;
    Unit part;
    part.start = begin;
    if (source[i] == '\\' && i + 1 < source.size()) {
      const char escaped = source[i + 1];
      if (escaped == '\\') {
        part.display = "\\";
        i += 2;
      } else if (escaped == 'n' || escaped == 'r') {
        i += 2;
        // An escaped CRLF pair is one break, not two.
        if (escaped == 'r' && i + 1 < source.size() &&
            source[i] == '\\' && source[i + 1] == 'n')
          i += 2;
        part.kind = Unit::Kind::Newline;
      } else if (escaped == 't') {
        part.display = "\t";
        part.kind = Unit::Kind::Space;
        i += 2;
      } else {
        // Keep unknown escape syntax literal rather than dropping data.
        part.display = "\\";
        ++i;
      }
    } else if (source[i] == '\r' || source[i] == '\n') {
      const bool crlf = source[i] == '\r' && i + 1 < source.size() &&
                        source[i + 1] == '\n';
      i += crlf ? 2 : 1;
      part.kind = Unit::Kind::Newline;
    } else {
      i = nextUtf8(source, i);
      part.display = source.substr(begin, i - begin);
      const unsigned char lead = static_cast<unsigned char>(source[begin]);
      if (part.display == " " || part.display == "\t")
        part.kind = Unit::Kind::Space;
      else if (lead >= 0xe0u && i > begin + 1)
        part.kind = Unit::Kind::Cjk;
    }
    part.end = i;
    result.push_back(std::move(part));
  }
  return result;
}

inline Layout layout(const std::string &source,
                     const std::function<float(const std::string &)> &measure,
                     float width) {
  Layout lines;
  if (source.empty())
    return lines;
  Line current;
  std::vector<Unit> group;
  const bool wrap = std::isfinite(width) && width > 0.0f;
  auto append = [&](const std::vector<Unit> &pieces) {
    if (pieces.empty())
      return;
    std::string addition;
    for (const auto &piece : pieces)
      addition += piece.display;
    if (wrap && !current.units.empty() &&
        measure(current.text + addition) > width) {
      lines.push_back(std::move(current));
      current = Line{};
      current.start = pieces.front().start;
    }
    current.text += addition;
    current.units.insert(current.units.end(), pieces.begin(), pieces.end());
  };
  auto flush = [&] {
    append(group);
    group.clear();
  };
  Unit::Kind groupKind = Unit::Kind::Word;
  for (auto &part : tokenize(source)) {
    if (part.kind == Unit::Kind::Newline) {
      flush();
      current.newlineEnd = part.end;
      lines.push_back(std::move(current));
      current = Line{};
      current.start = part.end;
      continue;
    }
    if (part.kind == Unit::Kind::Cjk) {
      flush();
      append({part});
      continue;
    }
    if (!group.empty() && part.kind != groupKind)
      flush();
    groupKind = part.kind;
    group.push_back(std::move(part));
  }
  flush();
  if (!current.units.empty())
    lines.push_back(std::move(current));
  return lines;
}

inline std::vector<std::string> allLines(const Layout &layout) {
  std::vector<std::string> result;
  result.reserve(layout.size());
  for (const auto &line : layout)
    result.push_back(line.text);
  return result;
}

inline std::vector<std::string> visibleLines(const Layout &layout,
                                              std::size_t sourceBytes) {
  std::vector<std::string> visible;
  visible.reserve(layout.size());
  for (const auto &line : layout) {
    if (line.start >= sourceBytes && line.newlineEnd == 0)
      break;
    std::string shown;
    for (const auto &unit : line.units) {
      if (unit.end > sourceBytes)
        break;
      shown += unit.display;
    }
    if (shown.empty()) {
      if (line.newlineEnd != 0 && line.newlineEnd <= sourceBytes)
        visible.emplace_back();
      else
        break;
    } else {
      visible.push_back(std::move(shown));
    }
  }
  return visible;
}

inline std::vector<std::string>
wrap(const std::string &source,
     const std::function<float(const std::string &)> &measure, float width) {
  return allLines(layout(source, measure, width));
}

inline std::vector<std::string>
reveal(const std::string &source, std::size_t visibleBytes,
       const std::function<float(const std::string &)> &measure, float width) {
  return visibleLines(layout(source, measure, width),
                      std::min(source.size(), visibleBytes));
}

} // namespace Kotonoha::TextWrapping
