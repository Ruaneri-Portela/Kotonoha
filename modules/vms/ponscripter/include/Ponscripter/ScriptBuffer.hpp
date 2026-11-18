// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Ponscripter/PonscripterTypes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ponscripter {

struct ScriptDirectives {
  Resolution resolution{640, 480};
  int globalVariableBorder = 200;
  std::string gameId;
};

struct StatementSlice {
  std::size_t lineIndex = 0;      // 1-based line number in concatenated buffer
  std::size_t statementIndex = 0; // 0-based index in statements()
  std::string text;               // trimmed statement text
  bool isLabel = false;
  std::string labelName;          // normalized lowercase without leading '*'
  bool isText = false;            // starts with '^', '`', or non-ASCII text
};

class ScriptBuffer {
public:
  ScriptBuffer() = default;

  void clear();

  // Appends a UTF-8 script file (stripping UTF-8 BOM EF BB BF at byte 0).
  void appendFile(std::string_view fileName, std::string_view utf8Content);

  // Parses header directives (;mode800, ;valueN, ;gameid) and indexes all
  // statements and *labels.
  void finalize();

  const ScriptDirectives &directives() const { return directives_; }
  const std::vector<StatementSlice> &statements() const { return statements_; }

  // Looks up a label (with or without leading '*', case-insensitive).
  // Returns statementIndex or npos if not found.
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);
  std::size_t findLabel(std::string_view label) const;
  bool hasLabel(std::string_view label) const {
    return findLabel(label) != npos;
  }

  static std::string stripBom(std::string_view raw);
  static std::string normalizeLabel(std::string_view raw);

private:
  ScriptDirectives directives_;
  bool firstFileAdded_ = false;
  std::vector<std::string> rawLines_;
  std::vector<StatementSlice> statements_;
  std::unordered_map<std::string, std::size_t> labelIndex_;
};

} // namespace Ponscripter
