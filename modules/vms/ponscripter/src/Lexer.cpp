// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/Lexer.hpp>
#include <Ponscripter/PonscripterTypes.hpp>
#include <Ponscripter/ScriptBuffer.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Ponscripter {
namespace {

bool isIdentStart(char ch) {
  const unsigned char c = static_cast<unsigned char>(ch);
  return std::isalpha(c) || c == '_';
}

bool isIdentChar(char ch) {
  const unsigned char c = static_cast<unsigned char>(ch);
  return std::isalnum(c) || c == '_';
}

} // namespace

LineLexer::LineLexer(std::string_view src, const VariableStore *vars)
    : src_(src), vars_(vars) {}

void LineLexer::skipWhitespace() {
  while (pos_ < src_.size() &&
         std::isspace(static_cast<unsigned char>(src_[pos_])))
    ++pos_;
}

bool LineLexer::eof() const { return pos_ >= src_.size(); }

char LineLexer::peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }

char LineLexer::get() { return pos_ < src_.size() ? src_[pos_++] : '\0'; }

std::string_view LineLexer::remaining() const {
  return pos_ <= src_.size() ? src_.substr(pos_) : std::string_view{};
}

bool LineLexer::consumeComma() {
  skipWhitespace();
  if (pos_ < src_.size() && src_[pos_] == ',') {
    ++pos_;
    skipWhitespace();
    return true;
  }
  return false;
}

std::string LineLexer::parseIdentifier() {
  skipWhitespace();
  const std::size_t start = pos_;
  while (pos_ < src_.size() && isIdentChar(src_[pos_]))
    ++pos_;
  std::string ident(src_.substr(start, pos_ - start));
  std::transform(
      ident.begin(), ident.end(), ident.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ident;
}

int LineLexer::parseVarRef() {
  skipWhitespace();
  if (pos_ >= src_.size() || src_[pos_] != '%')
    return -1;
  int indirection = 0;
  while (pos_ < src_.size() && src_[pos_] == '%') {
    ++indirection;
    ++pos_;
  }
  skipWhitespace();
  const std::size_t start = pos_;
  while (pos_ < src_.size() && isIdentChar(src_[pos_]))
    ++pos_;
  const std::string_view token = src_.substr(start, pos_ - start);
  if (token.empty())
    return -1;

  int id = -1;
  if (std::isdigit(static_cast<unsigned char>(token.front()))) {
    auto [p, ec] =
        std::from_chars(token.data(), token.data() + token.size(), id);
    (void)p;
    if (ec != std::errc{} || p != token.data() + token.size())
      return -1;
  } else if (vars_) {
    if (!vars_->findNumAlias(token, id))
      return -1;
  }
  if (vars_) {
    for (int i = 1; i < indirection; ++i)
      id = vars_->getNum(id);
  }
  return id;
}

int LineLexer::parseStrVarRef() {
  skipWhitespace();
  if (pos_ >= src_.size() || src_[pos_] != '$')
    return -1;
  ++pos_;
  skipWhitespace();
  if (pos_ < src_.size() && src_[pos_] == '%') {
    const int refVar = parseVarRef();
    return (vars_ && refVar >= 0) ? vars_->getNum(refVar) : -1;
  }
  const std::size_t start = pos_;
  while (pos_ < src_.size() && isIdentChar(src_[pos_]))
    ++pos_;
  const std::string_view token = src_.substr(start, pos_ - start);
  if (token.empty())
    return -1;
  int id = -1;
  if (std::isdigit(static_cast<unsigned char>(token.front()))) {
    auto [p, ec] =
        std::from_chars(token.data(), token.data() + token.size(), id);
    (void)p;
    return (ec == std::errc{} && p == token.data() + token.size()) ? id : -1;
  }
  if (vars_ && vars_->findNumAlias(token, id))
    return id;
  return -1;
}

std::string LineLexer::parseLabelTarget() {
  skipWhitespace();
  if (pos_ < src_.size() && src_[pos_] == '*') {
    ++pos_;
    const std::size_t start = pos_;
    while (pos_ < src_.size() &&
           !std::isspace(static_cast<unsigned char>(src_[pos_])) &&
           src_[pos_] != ',' && src_[pos_] != ':') {
      ++pos_;
    }
    return ScriptBuffer::normalizeLabel(src_.substr(start, pos_ - start));
  }
  return ScriptBuffer::normalizeLabel(parseStringExpr());
}

std::string LineLexer::parseStringExpr() {
  auto parseSingleStringTerm = [&]() -> std::string {
    skipWhitespace();
    if (pos_ >= src_.size())
      return {};
    if (src_[pos_] == '"') {
      ++pos_;
      std::string out;
      while (pos_ < src_.size() && src_[pos_] != '"') {
        out.push_back(src_[pos_++]);
      }
      if (pos_ < src_.size() && src_[pos_] == '"')
        ++pos_;
      return out;
    }
    if (src_[pos_] == '^') {
      ++pos_;
      std::string out;
      while (pos_ < src_.size() && src_[pos_] != '^' && src_[pos_] != ',') {
        out.push_back(src_[pos_++]);
      }
      if (pos_ < src_.size() && src_[pos_] == '^')
        ++pos_;
      return out;
    }
    if (src_[pos_] == '$') {
      const int id = parseStrVarRef();
      return (vars_ && id >= 0) ? vars_->getStr(id) : std::string{};
    }
    // Bareword stralias or unquoted token (e.g. `s02`, `black`, `white`,
    // `#ffffff`)
    const std::size_t start = pos_;
    while (pos_ < src_.size() && src_[pos_] != ',' && src_[pos_] != ':' &&
           src_[pos_] != '+' &&
           !std::isspace(static_cast<unsigned char>(src_[pos_]))) {
      ++pos_;
    }
    const std::string_view token = src_.substr(start, pos_ - start);
    if (token.empty())
      return {};
    std::string resolved;
    if (vars_ && vars_->findStrAlias(token, resolved))
      return resolved;
    return std::string(token);
  };

  std::string result = parseSingleStringTerm();
  while (true) {
    skipWhitespace();
    if (pos_ < src_.size() && src_[pos_] == '+') {
      ++pos_;
      result += parseSingleStringTerm();
    } else {
      break;
    }
  }
  return result;
}

int LineLexer::parsePrimaryInt() {
  skipWhitespace();
  if (pos_ >= src_.size())
    return 0;

  if (src_[pos_] == '(') {
    ++pos_;
    const int val = parseIntExpr();
    skipWhitespace();
    if (pos_ < src_.size() && src_[pos_] == ')')
      ++pos_;
    return val;
  }

  if (src_[pos_] == '-') {
    ++pos_;
    return -parsePrimaryInt();
  }
  if (src_[pos_] == '+') {
    ++pos_;
    return parsePrimaryInt();
  }

  if (src_[pos_] == '%') {
    const int id = parseVarRef();
    return (vars_ && id >= 0) ? vars_->getNum(id) : 0;
  }

  if (src_[pos_] == '?') {
    ++pos_;
    int arrId = 0;
    const std::size_t start = pos_;
    while (pos_ < src_.size() && isIdentChar(src_[pos_]))
      ++pos_;
    const std::string_view token = src_.substr(start, pos_ - start);
    if (!token.empty()) {
      if (std::isdigit(static_cast<unsigned char>(token.front()))) {
        std::from_chars(token.data(), token.data() + token.size(), arrId);
      } else if (vars_) {
        vars_->findNumAlias(token, arrId);
      }
    }
    std::vector<int> indices;
    while (pos_ < src_.size() && src_[pos_] == '[') {
      ++pos_;
      indices.push_back(parseIntExpr());
      skipWhitespace();
      if (pos_ < src_.size() && src_[pos_] == ']')
        ++pos_;
    }
    return vars_ ? vars_->getArray(arrId, indices) : 0;
  }

  if (src_[pos_] == '#') {
    ++pos_;
    const std::size_t start = pos_;
    while (pos_ < src_.size() &&
           std::isxdigit(static_cast<unsigned char>(src_[pos_])))
      ++pos_;
    std::uint32_t rgb = 0;
    if (parseHexColor24(src_.substr(start, pos_ - start), rgb))
      return static_cast<int>(rgb);
    return 0;
  }

  if (std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
    const std::size_t start = pos_;
    while (pos_ < src_.size() &&
           std::isdigit(static_cast<unsigned char>(src_[pos_])))
      ++pos_;
    int val = 0;
    std::from_chars(src_.data() + start, src_.data() + pos_, val);
    return val;
  }

  if (isIdentStart(src_[pos_])) {
    const std::size_t start = pos_;
    while (pos_ < src_.size() && isIdentChar(src_[pos_]))
      ++pos_;
    const std::string_view ident = src_.substr(start, pos_ - start);
    int val = 0;
    if (vars_ && vars_->findNumAlias(ident, val))
      return val;
    return 0;
  }

  return 0;
}

int LineLexer::parseMulDivInt() {
  int left = parsePrimaryInt();
  while (true) {
    skipWhitespace();
    if (pos_ >= src_.size())
      break;
    if (src_[pos_] == '*') {
      // Make sure it's not a label target like `*accepted`
      if (pos_ + 1 < src_.size() && isIdentStart(src_[pos_ + 1])) {
        // Wait: in an arithmetic expression `a * b` vs `if %0=1 *label`?
        // In PONscripter, a bare `*label` is not a command, only `goto *label`
        // or `gosub *label`. So `*` after an int expression is multiplication!
      }
      ++pos_;
      left *= parsePrimaryInt();
    } else if (src_[pos_] == '/') {
      ++pos_;
      const int right = parsePrimaryInt();
      left = (right != 0) ? (left / right) : 0;
    } else if (src_.substr(pos_, 3) == "mod" &&
               (pos_ + 3 == src_.size() || !isIdentChar(src_[pos_ + 3]))) {
      pos_ += 3;
      const int right = parsePrimaryInt();
      left = (right != 0) ? (left % right) : 0;
    } else {
      break;
    }
  }
  return left;
}

int LineLexer::parseIntExpr() {
  int left = parseMulDivInt();
  while (true) {
    skipWhitespace();
    if (pos_ >= src_.size())
      break;
    if (src_[pos_] == '+') {
      ++pos_;
      left += parseMulDivInt();
    } else if (src_[pos_] == '-') {
      ++pos_;
      left -= parseMulDivInt();
    } else {
      break;
    }
  }
  return left;
}

bool LineLexer::parseSingleComparison(
    const std::function<bool(std::string_view)> &fchkCallback,
    const std::function<bool(std::string_view)> &lchkCallback) {
  skipWhitespace();
  // Check `fchk "path"` or `lchk *label`
  const std::size_t savedPos = pos_;
  if (pos_ < src_.size() && isIdentStart(src_[pos_])) {
    const std::string word = parseIdentifier();
    if (word == "fchk") {
      const std::string path = parseStringExpr();
      return fchkCallback ? fchkCallback(path) : false;
    }
    if (word == "lchk") {
      const std::string lbl = parseLabelTarget();
      return lchkCallback ? lchkCallback(lbl) : false;
    }
    pos_ = savedPos;
  }

  // String comparison if starting with '$' or '"'
  if (pos_ < src_.size() && (src_[pos_] == '$' || src_[pos_] == '"')) {
    const std::string lhs = parseStringExpr();
    skipWhitespace();
    std::string op;
    if (pos_ + 1 < src_.size()) {
      const std::string_view two = src_.substr(pos_, 2);
      if (two == "==" || two == "!=" || two == "<>") {
        op = std::string(two);
        pos_ += 2;
      }
    }
    if (op.empty() && pos_ < src_.size() && src_[pos_] == '=') {
      op = "=";
      ++pos_;
    }
    const std::string rhs = parseStringExpr();
    if (op == "!=" || op == "<>")
      return lhs != rhs;
    return lhs == rhs;
  }

  const int lhs = parseIntExpr();
  skipWhitespace();

  std::string op;
  if (pos_ + 1 < src_.size()) {
    const std::string_view two = src_.substr(pos_, 2);
    if (two == "==" || two == "!=" || two == "<>" || two == "<=" ||
        two == ">=") {
      op = std::string(two);
      pos_ += 2;
    }
  }
  if (op.empty() && pos_ < src_.size() &&
      (src_[pos_] == '=' || src_[pos_] == '<' || src_[pos_] == '>')) {
    op.push_back(src_[pos_++]);
  }

  if (op.empty())
    return lhs != 0;

  const int rhs = parseIntExpr();
  if (op == "=" || op == "==")
    return lhs == rhs;
  if (op == "!=" || op == "<>")
    return lhs != rhs;
  if (op == "<")
    return lhs < rhs;
  if (op == "<=")
    return lhs <= rhs;
  if (op == ">")
    return lhs > rhs;
  if (op == ">=")
    return lhs >= rhs;
  return false;
}

bool LineLexer::parseCondition(
    const std::function<bool(std::string_view)> &fchkCallback,
    const std::function<bool(std::string_view)> &lchkCallback) {
  bool result = parseSingleComparison(fchkCallback, lchkCallback);
  while (true) {
    skipWhitespace();
    if (pos_ >= src_.size())
      break;
    if (pos_ + 1 < src_.size() && src_.substr(pos_, 2) == "&&") {
      pos_ += 2;
      const bool rhs = parseSingleComparison(fchkCallback, lchkCallback);
      result = result && rhs;
    } else if (src_[pos_] == '&') {
      ++pos_;
      const bool rhs = parseSingleComparison(fchkCallback, lchkCallback);
      result = result && rhs;
    } else if (pos_ + 1 < src_.size() && src_.substr(pos_, 2) == "||") {
      pos_ += 2;
      const bool rhs = parseSingleComparison(fchkCallback, lchkCallback);
      result = result || rhs;
    } else {
      break;
    }
  }
  return result;
}

std::string LineLexer::parseRawArg() {
  skipWhitespace();
  if (pos_ >= src_.size())
    return {};
  if (src_[pos_] == '"')
    return parseStringExpr();
  const std::size_t start = pos_;
  while (pos_ < src_.size() && src_[pos_] != ',' && src_[pos_] != ':') {
    ++pos_;
  }
  std::size_t end = pos_;
  while (end > start && std::isspace(static_cast<unsigned char>(src_[end - 1])))
    --end;
  return std::string(src_.substr(start, end - start));
}

} // namespace Ponscripter
