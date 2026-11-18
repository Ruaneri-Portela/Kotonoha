// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Ponscripter/PonscripterTypes.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Ponscripter {

enum class TextTerminator : std::uint8_t {
  None = 0,       // End of line without wait (implicit newline unless '/')
  Slash,          // '/' — continue onto next line without newline or wait
  ClickInline,    // '@' — wait for click inline, keep page
  ClickPageClear  // '\' — wait for click, then push backlog & clear page
};

struct FormattedTextLine {
  std::vector<TextSpan> spans;
  std::string plainText; // stripped plain UTF-8 text for backlog / accessibility
  TextTerminator terminator = TextTerminator::None;
  int inlineDelayMs = 0; // if '!d<ms>' or '!w<ms>' encountered
  bool inlineDelaySkippable = true;
};

class TextFormatter {
public:
  // Formats a PONscripter text line (starting with '^' or '`'), maintaining
  // and updating `ioStyle` across lines on the same page.
  static FormattedTextLine formatLine(std::string_view rawLine,
                                      TextStyle &ioStyle,
                                      bool enableLigatures = true);

  // Strips PONscripter leading '^' and inline tags (~i~, ~~, #RRGGBB) for UI
  // labels like rmenu ("^~~ Salvar ~~" -> "~ Salvar ~") and select options.
  static std::string formatUiLabel(std::string_view rawLabel,
                                   bool enableLigatures = true);
};

} // namespace Ponscripter
