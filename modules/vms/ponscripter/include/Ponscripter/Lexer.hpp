// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Ponscripter/VariableStore.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Ponscripter {

class LineLexer {
public:
  explicit LineLexer(std::string_view src, const VariableStore *vars = nullptr);

  void skipWhitespace();
  bool eof() const;
  char peek() const;
  char get();

  std::size_t position() const { return pos_; }
  void setPosition(std::size_t pos) { pos_ = pos; }
  std::string_view remaining() const;

  bool consumeComma();

  // Parses command name or bareword identifier
  std::string parseIdentifier();

  // Parses a variable target (e.g. `%0`, `%volmp3`, `%111`, `%%0`).
  // Returns resolved variable ID (0..4095) or -1 if invalid.
  int parseVarRef();

  // Parses a string variable target (e.g. `$0`, `$name`).
  int parseStrVarRef();

  // Parses a label target (e.g. `*start`, `*001`, or string expression).
  std::string parseLabelTarget();

  // Parses a string expression: quoted `"..."`, `^...^`, `$var`, or `stralias`
  // bareword, supporting `+` concatenation.
  std::string parseStringExpr();

  // Parses an integer expression: literals, `#RRGGBB`, `%var`, `numalias`,
  // unary `+/-`, `*`, `/`, `mod`, `+`, `-`, parentheses.
  int parseIntExpr();

  // Parses a conditional expression for `if` / `notif`:
  // supports `=`, `==`, `!=`, `<>`, `<`, `<=`, `>`, `>=`, `fchk`, `lchk`,
  // chained with `&&` / `&` and `||`.
  bool parseCondition(
      const std::function<bool(std::string_view)> &fchkCallback = nullptr,
      const std::function<bool(std::string_view)> &lchkCallback = nullptr);

  // Parses raw token up to next comma or end of statement (used for unquoted
  // color tokens like `black`, `white`, `#ffffff`, or barewords like `save`).
  std::string parseRawArg();

private:
  std::string_view src_;
  std::size_t pos_ = 0;
  const VariableStore *vars_ = nullptr;

  int parsePrimaryInt();
  int parseMulDivInt();
  bool parseSingleComparison(
      const std::function<bool(std::string_view)> &fchkCallback,
      const std::function<bool(std::string_view)> &lchkCallback);
};

} // namespace Ponscripter
