// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/TextFormatter.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Ponscripter {
namespace {

void parseTildeTagContent(std::string_view content, TextStyle &style) {
  std::size_t pos = 0;
  while (pos < content.size()) {
    while (pos < content.size() &&
           std::isspace(static_cast<unsigned char>(content[pos])))
      ++pos;
    if (pos >= content.size())
      break;
    const char cmd = static_cast<char>(
        std::tolower(static_cast<unsigned char>(content[pos++])));
    switch (cmd) {
    case 'i':
      style.italic = !style.italic;
      break;
    case 'b':
      style.bold = !style.bold;
      break;
    case 'u':
      style.underline = !style.underline;
      break;
    case 'n':
      style.italic = false;
      style.bold = false;
      style.underline = false;
      break;
    case 'd':
      style = TextStyle{};
      break;
    case 'f': {
      int slot = 0;
      const std::size_t s = pos;
      while (pos < content.size() &&
             std::isdigit(static_cast<unsigned char>(content[pos])))
        ++pos;
      if (pos > s) {
        std::from_chars(content.data() + s, content.data() + pos, slot);
        style.fontSlot = std::clamp(slot, 0, 7);
      }
      break;
    }
    case 'c': {
      if (pos < content.size() && content[pos] == '#')
        ++pos;
      if (pos + 6 <= content.size()) {
        std::uint32_t rgb = 0;
        if (parseHexColor24(content.substr(pos, 6), rgb)) {
          style.colorRgb = rgb;
          pos += 6;
        }
      }
      break;
    }
    case 's':
    case '=': {
      int sz = 0;
      const std::size_t s = pos;
      while (pos < content.size() &&
             std::isdigit(static_cast<unsigned char>(content[pos])))
        ++pos;
      if (pos > s) {
        std::from_chars(content.data() + s, content.data() + pos, sz);
        style.fontSizePx = std::max(0, sz);
      }
      break;
    }
    case '+':
    case '-':
    case '%':
    case 'x':
    case 'y': {
      while (pos < content.size() &&
             (std::isdigit(static_cast<unsigned char>(content[pos])) ||
              content[pos] == '+' || content[pos] == '-'))
        ++pos;
      break;
    }
    default:
      break;
    }
  }
}

} // namespace

FormattedTextLine TextFormatter::formatLine(std::string_view rawLine,
                                            TextStyle &ioStyle,
                                            bool enableLigatures) {
  FormattedTextLine out;
  std::size_t pos = 0;
  while (pos < rawLine.size() &&
         std::isspace(static_cast<unsigned char>(rawLine[pos])))
    ++pos;

  // Strip leading '^' or '`'
  if (pos < rawLine.size() && (rawLine[pos] == '^' || rawLine[pos] == '`'))
    ++pos;

  std::string currentRun;
  auto flushRun = [&]() {
    if (currentRun.empty())
      return;
    TextSpan span;
    span.text = currentRun;
    span.style = ioStyle;
    out.plainText += currentRun;
    out.spans.push_back(std::move(span));
    currentRun.clear();
  };

  while (pos < rawLine.size()) {
    const char ch = rawLine[pos];

    // Check '#' escape or inline #RRGGBB color
    if (ch == '#') {
      if (pos + 1 < rawLine.size()) {
        const char next = rawLine[pos + 1];
        if (next == '@' || next == '/' || next == '\\' || next == '_' ||
            next == '^' || next == '`' || next == '!' || next == '#') {
          currentRun.push_back(next);
          pos += 2;
          continue;
        }
      }
      if (pos + 7 <= rawLine.size()) {
        std::uint32_t rgb = 0;
        if (parseHexColor24(rawLine.substr(pos + 1, 6), rgb)) {
          flushRun();
          ioStyle.colorRgb = rgb;
          pos += 7;
          continue;
        }
      }
      currentRun.push_back(ch);
      ++pos;
      continue;
    }

    // Check '~' inline tags and '~~' literal tilde escape
    if (ch == '~') {
      if (pos + 1 < rawLine.size() && rawLine[pos + 1] == '~') {
        currentRun.push_back('~');
        pos += 2;
        continue;
      }
      const std::size_t closeTilde = rawLine.find('~', pos + 1);
      if (closeTilde != std::string_view::npos) {
        flushRun();
        parseTildeTagContent(rawLine.substr(pos + 1, closeTilde - (pos + 1)),
                             ioStyle);
        pos = closeTilde + 1;
        continue;
      }
      currentRun.push_back(ch);
      ++pos;
      continue;
    }

    // Check inline '!' speed/delay commands (!s<N>, !sd, !d<ms>, !w<ms>)
    if (ch == '!' && pos + 1 < rawLine.size()) {
      const char next = static_cast<char>(
          std::tolower(static_cast<unsigned char>(rawLine[pos + 1])));
      if (next == 's') {
        pos += 2;
        if (pos < rawLine.size() &&
            (rawLine[pos] == 'd' || rawLine[pos] == 'D')) {
          ++pos;
        } else {
          while (pos < rawLine.size() &&
                 std::isdigit(static_cast<unsigned char>(rawLine[pos])))
            ++pos;
        }
        continue;
      }
      if (next == 'd' || next == 'w') {
        out.inlineDelaySkippable = (next == 'd');
        pos += 2;
        const std::size_t s = pos;
        while (pos < rawLine.size() &&
               std::isdigit(static_cast<unsigned char>(rawLine[pos])))
          ++pos;
        if (pos > s) {
          std::from_chars(rawLine.data() + s, rawLine.data() + pos,
                          out.inlineDelayMs);
        }
        continue;
      }
    }

    // Check smart ligatures: `...` -> `…`, `---` -> `—`, `--` -> `–`, `` -> “,
    // '' -> ”
    if (enableLigatures) {
      if (ch == '.' && pos + 2 < rawLine.size() && rawLine[pos + 1] == '.' &&
          rawLine[pos + 2] == '.') {
        currentRun += "\xE2\x80\xA6"; // U+2026
        pos += 3;
        continue;
      }
      if (ch == '-' && pos + 2 < rawLine.size() && rawLine[pos + 1] == '-' &&
          rawLine[pos + 2] == '-') {
        currentRun += "\xE2\x80\x94"; // U+2014
        pos += 3;
        continue;
      }
      if (ch == '-' && pos + 1 < rawLine.size() && rawLine[pos + 1] == '-') {
        currentRun += "\xE2\x80\x93"; // U+2013
        pos += 2;
        continue;
      }
      if (ch == '`' && pos + 1 < rawLine.size() && rawLine[pos + 1] == '`') {
        currentRun += "\xE2\x80\x9C"; // U+201C
        pos += 2;
        continue;
      }
      if (ch == '\'' && pos + 1 < rawLine.size() && rawLine[pos + 1] == '\'') {
        currentRun += "\xE2\x80\x9D"; // U+201D
        pos += 2;
        continue;
      }
    }

    // Check line/page terminators: `@`, `\`, `/`
    // Note: in `00.utf:264`, `@` appears at the end of line or mid-line (`^...
    // @ Nós...`). If `@` is at the end (or only followed by whitespace), it
    // terminates the line with `ClickInline`. If `@` is mid-line, in
    // PONscripter `@` pauses inline and continues; for simplicity, if `@` is at
    // the end of the line, set `ClickInline`; if mid-line, we keep the rest of
    // the line in the same text statement and end with the final terminator.
    if (ch == '@' || ch == '\\' || ch == '/') {
      std::size_t rem = pos + 1;
      while (rem < rawLine.size() &&
             std::isspace(static_cast<unsigned char>(rawLine[rem])))
        ++rem;
      if (rem == rawLine.size()) {
        if (ch == '@')
          out.terminator = TextTerminator::ClickInline;
        else if (ch == '\\')
          out.terminator = TextTerminator::ClickPageClear;
        else
          out.terminator = TextTerminator::Slash;
        break;
      }
      if (ch == '@') {
        // Mid-line '@' wait marker (e.g. `00.utf:1576`): strip '@' and continue
        ++pos;
        continue;
      }
    }

    // Check closing '^' if present at end of line
    if (ch == '^') {
      std::size_t rem = pos + 1;
      while (rem < rawLine.size() &&
             std::isspace(static_cast<unsigned char>(rawLine[rem])))
        ++rem;
      if (rem == rawLine.size())
        break;
    }

    currentRun.push_back(ch);
    ++pos;
  }

  flushRun();
  return out;
}

std::string TextFormatter::formatUiLabel(std::string_view rawLabel,
                                         bool enableLigatures) {
  TextStyle style;
  FormattedTextLine formatted = formatLine(rawLabel, style, enableLigatures);
  return formatted.plainText;
}

} // namespace Ponscripter
