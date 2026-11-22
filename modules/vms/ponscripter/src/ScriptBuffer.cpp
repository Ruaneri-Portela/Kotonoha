// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/ScriptBuffer.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Ponscripter {
namespace {

std::string trim(std::string_view sv) {
  std::size_t start = 0;
  while (start < sv.size() &&
         std::isspace(static_cast<unsigned char>(sv[start])))
    ++start;
  std::size_t end = sv.size();
  while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])))
    --end;
  return std::string(sv.substr(start, end - start));
}

bool isTextStart(std::string_view trimmed) {
  if (trimmed.empty())
    return false;
  const unsigned char c = static_cast<unsigned char>(trimmed.front());
  return c == '^' || c == '`' || c >= 0x80u;
}

// Splits a single script line into colon-separated statements, while
// respecting:
// - `;` comments outside strings and text lines
// - `"..."` quoted strings
// - `^...` or `` `... `` text lines (which consume the rest of the line!)
// - `:a;...`, `:c/...`, `:l/...`, `:s...` sprite tags (where `:` is preceded by
//   whitespace/comma/`"` rather than ending a statement, wait: in PONscripter,
//   sprite tags inside `lsp`/`ld`/`setcursor`/`setwindow3`/`btndef` are ALWAYS
//   inside `"..."` quotes! E.g. `lsp 85,":a;system\ffffff.png",0,0:print 1`.
//   Because the `:` of `:a;` is inside `"..."`, tracking `inQuotes` handles it
//   100% cleanly!)
void splitLineIntoStatements(std::string_view line, std::size_t lineNumber,
                             std::vector<StatementSlice> &out) {
  std::string cleaned = trim(line);
  if (cleaned.empty() || cleaned.front() == ';')
    return;

  // If the entire line starts with '^' or '`', the whole line is a single text
  // statement (comments after ';' do NOT apply inside text lines!).
  if (isTextStart(cleaned)) {
    StatementSlice slice;
    slice.lineIndex = lineNumber;
    slice.statementIndex = out.size();
    slice.text = cleaned;
    slice.isText = true;
    out.push_back(std::move(slice));
    return;
  }

  bool inQuotes = false;
  std::size_t segStart = 0;

  auto flushSegment = [&](std::size_t segEnd) {
    if (segEnd <= segStart)
      return;
    std::string seg = trim(line.substr(segStart, segEnd - segStart));
    if (seg.empty())
      return;
    StatementSlice slice;
    slice.lineIndex = lineNumber;
    slice.statementIndex = out.size();
    if (seg.front() == '*') {
      slice.isLabel = true;
      slice.labelName = ScriptBuffer::normalizeLabel(seg);
      slice.text = std::move(seg);
    } else if (isTextStart(seg)) {
      slice.isText = true;
      slice.text = std::move(seg);
    } else {
      slice.text = std::move(seg);
    }
    out.push_back(std::move(slice));
  };

  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch == '"') {
      inQuotes = !inQuotes;
      continue;
    }
    if (!inQuotes) {
      // Check if a bare '^' text literal starts mid-line (rare, e.g. after ':')
      // Note: inside `rmenu "^~~ Salvar ~~"` or `select "^Concordo"`, `^` is
      // inside quotes `"..."`, so `inQuotes` is true!
      if (ch == ';') {
        flushSegment(i);
        return;
      }
      if (ch == ':') {
        flushSegment(i);
        segStart = i + 1;
        // Check if the remainder after ':' starts with '^' or '`'
        std::size_t peek = segStart;
        while (peek < line.size() &&
               std::isspace(static_cast<unsigned char>(line[peek])))
          ++peek;
        if (peek < line.size() && isTextStart(line.substr(peek))) {
          flushSegment(line.size());
          return;
        }
      }
    }
  }
  flushSegment(line.size());
}

} // namespace

void ScriptBuffer::clear() {
  directives_ = ScriptDirectives{};
  firstFileAdded_ = false;
  rawLines_.clear();
  statements_.clear();
  labelIndex_.clear();
}

std::string ScriptBuffer::stripBom(std::string_view raw) {
  if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEFu &&
      static_cast<unsigned char>(raw[1]) == 0xBBu &&
      static_cast<unsigned char>(raw[2]) == 0xBFu) {
    raw.remove_prefix(3);
  }
  return std::string(raw);
}

std::string ScriptBuffer::normalizeLabel(std::string_view raw) {
  std::string s = trim(raw);
  if (!s.empty() && s.front() == '*')
    s.erase(s.begin());
  s = trim(s);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

void ScriptBuffer::appendFile(std::string_view fileName,
                              std::string_view utf8Content) {
  (void)fileName;
  std::string content = stripBom(utf8Content);

  std::size_t pos = 0;
  std::size_t lineIndexInFile = 0;
  while (pos <= content.size()) {
    const std::size_t nl = content.find('\n', pos);
    std::string line = (nl == std::string::npos)
                           ? content.substr(pos)
                           : content.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    // Strip any embedded BOM at start of line 0 just in case
    if (lineIndexInFile == 0)
      line = stripBom(line);

    // Check header directives on early comment lines of the first script file
    if (!firstFileAdded_ && lineIndexInFile < 16) {
      std::string trimmedLine = trim(line);
      if (!trimmedLine.empty() && trimmedLine.front() == ';') {
        std::string lower = trimmedLine.substr(1);
        std::transform(
            lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Directives may be separated by commas on line 1 (e.g.
        // `;mode800,value500`)
        std::size_t dpos = 0;
        while (dpos < lower.size()) {
          const std::size_t comma = lower.find(',', dpos);
          std::string token =
              trim((comma == std::string::npos)
                       ? std::string_view(lower).substr(dpos)
                       : std::string_view(lower).substr(dpos, comma - dpos));
          if (token == "mode800") {
            directives_.resolution = {800, 600};
          } else if (token == "mode640") {
            directives_.resolution = {640, 480};
          } else if (token == "mode400") {
            directives_.resolution = {400, 300};
          } else if (token == "mode320") {
            directives_.resolution = {320, 240};
          } else if (token.rfind("value", 0) == 0) {
            int border = 200;
            const std::string numStr = trim(std::string_view(token).substr(5));
            auto [p, ec] = std::from_chars(
                numStr.data(), numStr.data() + numStr.size(), border);
            (void)p;
            if (ec == std::errc{} && border > 0)
              directives_.globalVariableBorder = border;
          } else if (token.rfind("gameid ", 0) == 0) {
            directives_.gameId = trim(std::string_view(trimmedLine).substr(8));
          }
          if (comma == std::string::npos)
            break;
          dpos = comma + 1;
        }
      }
    }

    rawLines_.push_back(std::move(line));
    ++lineIndexInFile;
    if (nl == std::string::npos)
      break;
    pos = nl + 1;
  }

  firstFileAdded_ = true;
}

void ScriptBuffer::finalize() {
  statements_.clear();
  labelIndex_.clear();
  statements_.reserve(rawLines_.size());

  for (std::size_t i = 0; i < rawLines_.size(); ++i) {
    const std::size_t before = statements_.size();
    splitLineIntoStatements(rawLines_[i], i + 1, statements_);
    for (std::size_t s = before; s < statements_.size(); ++s) {
      if (statements_[s].isLabel && !statements_[s].labelName.empty()) {
        // Keep first definition of a label if duplicated
        labelIndex_.emplace(statements_[s].labelName, s);
      }
    }
  }

  // Choice statements may span physical lines.  Fold only explicit
  // continuations into the original statement, before publishing PC indices.
  // A continuation is either announced by a trailing comma or begins with
  // a comma on the next line.  Colon-separated statements are left intact.
  auto isChoice = [](std::string_view statement) {
    const auto end = statement.find_first_of(" \t");
    const auto cmd = statement.substr(0, end);
    return cmd == "select" || cmd == "selgosub" || cmd == "selnum" ||
           cmd == "csel";
  };
  std::vector<StatementSlice> folded;
  folded.reserve(statements_.size());
  for (std::size_t i = 0; i < statements_.size(); ++i) {
    StatementSlice current = std::move(statements_[i]);
    if (!current.isLabel && !current.isText && isChoice(current.text)) {
      while (i + 1 < statements_.size()) {
        const auto &next = statements_[i + 1];
        if (next.isLabel || next.isText ||
            next.lineIndex <= current.lineIndex)
          break;
        const bool continued = !current.text.empty() &&
                               current.text.back() == ',';
        const bool leadingComma = !next.text.empty() &&
                                  next.text.front() == ',';
        if (!continued && !leadingComma)
          break;
        // Consecutive commas must not accidentally produce a valid choice.
        if (continued && leadingComma)
          break;
        if (leadingComma) {
          current.text += next.text;
        } else {
          current.text += next.text;
        }
        ++i;
      }
    }
    folded.push_back(std::move(current));
  }
  statements_ = std::move(folded);
  labelIndex_.clear();
  for (std::size_t i = 0; i < statements_.size(); ++i) {
    statements_[i].statementIndex = i;
    if (statements_[i].isLabel && !statements_[i].labelName.empty())
      labelIndex_.emplace(statements_[i].labelName, i);
  }
}

std::size_t ScriptBuffer::findLabel(std::string_view label) const {
  const std::string key = normalizeLabel(label);
  const auto it = labelIndex_.find(key);
  if (it == labelIndex_.end())
    return npos;
  return it->second;
}

} // namespace Ponscripter
