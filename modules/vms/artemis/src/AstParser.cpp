// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/AstParser.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Artemis {

const LuaValue *LuaTable::get(std::string_view key) const {
  auto it = fieldIndex.find(std::string(key));
  if (it == fieldIndex.end())
    return nullptr;
  return &fields[it->second].second;
}

std::string LuaTable::getString(std::string_view key,
                                std::string_view fallback) const {
  const auto *v = get(key);
  return v ? v->asString(fallback) : std::string(fallback);
}

double LuaTable::getNumber(std::string_view key, double fallback) const {
  const auto *v = get(key);
  return v ? v->asNumber(fallback) : fallback;
}

int LuaTable::getInt(std::string_view key, int fallback) const {
  const auto *v = get(key);
  return v ? v->asInt(fallback) : fallback;
}

bool LuaTable::getBool(std::string_view key, bool fallback) const {
  const auto *v = get(key);
  return v ? v->asBool(fallback) : fallback;
}

const LuaTable *LuaTable::getTable(std::string_view key) const {
  const auto *v = get(key);
  return (v && v->isTable()) ? v->tableVal.get() : nullptr;
}

bool LuaTable::has(std::string_view key) const {
  return fieldIndex.find(std::string(key)) != fieldIndex.end();
}

std::string LuaValue::asString(std::string_view fallback) const {
  if (type == Type::String)
    return strVal;
  if (type == Type::Number) {
    if (std::floor(numVal) == numVal)
      return std::to_string(static_cast<std::int64_t>(numVal));
    return std::to_string(numVal);
  }
  if (type == Type::Bool)
    return boolVal ? "true" : "false";
  return std::string(fallback);
}

double LuaValue::asNumber(double fallback) const {
  if (type == Type::Number)
    return numVal;
  if (type == Type::Bool)
    return boolVal ? 1.0 : 0.0;
  if (type == Type::String && !strVal.empty()) {
    char *end = nullptr;
    const double parsed = std::strtod(strVal.c_str(), &end);
    if (end && end != strVal.c_str())
      return parsed;
  }
  return fallback;
}

int LuaValue::asInt(int fallback) const {
  if (type == Type::Number)
    return static_cast<int>(numVal);
  if (type == Type::Bool)
    return boolVal ? 1 : 0;
  if (type == Type::String && !strVal.empty()) {
    char *end = nullptr;
    const long parsed = std::strtol(strVal.c_str(), &end, 10);
    if (end && end != strVal.c_str())
      return static_cast<int>(parsed);
  }
  return fallback;
}

bool LuaValue::asBool(bool fallback) const {
  if (type == Type::Bool)
    return boolVal;
  if (type == Type::Number)
    return numVal != 0.0;
  if (type == Type::String)
    return !strVal.empty() && strVal != "0" && strVal != "false";
  return fallback;
}

namespace {

class LuaTableReader {
public:
  explicit LuaTableReader(std::string_view src) : src_(src) {
    // Strip UTF-8 BOM if present.
    if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
        static_cast<unsigned char>(src_[1]) == 0xBB &&
        static_cast<unsigned char>(src_[2]) == 0xBF) {
      pos_ = 3;
    }
  }

  std::shared_ptr<LuaTable> parseTopLevel() {
    auto root = std::make_shared<LuaTable>();
    while (true) {
      skipTrivia();
      if (eof())
        break;
      // Optional `local` keyword.
      if (matchKeyword("local"))
        skipTrivia();
      std::string key;
      if (peek() == '[') {
        key = parseBracketKey();
      } else {
        key = parseIdent();
      }
      if (key.empty()) {
        // Recover from unexpected token.
        ++pos_;
        continue;
      }
      skipTrivia();
      if (peek() != '=') {
        continue;
      }
      ++pos_; // consume '='
      LuaValue val = parseValue();
      setField(*root, std::move(key), std::move(val));
      skipTrivia();
      if (peek() == ';')
        ++pos_;
    }
    return root;
  }

private:
  bool eof() const { return pos_ >= src_.size(); }
  char peek(std::size_t ahead = 0) const {
    return (pos_ + ahead < src_.size()) ? src_[pos_ + ahead] : '\0';
  }

  static bool isIdentStart(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) || c == '_' || uc >= 0x80;
  }

  static bool isIdentChar(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_' || uc >= 0x80;
  }

  void skipTrivia() {
    while (!eof()) {
      const char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++pos_;
        continue;
      }
      if (c == '-' && peek(1) == '-') {
        pos_ += 2;
        if (peek() == '[') {
          int eqCount = 0;
          std::size_t scan = pos_ + 1;
          while (scan < src_.size() && src_[scan] == '=') {
            ++eqCount;
            ++scan;
          }
          if (scan < src_.size() && src_[scan] == '[') {
            pos_ = scan + 1;
            skipLongBracketEnd(eqCount);
            continue;
          }
        }
        while (!eof() && peek() != '\n')
          ++pos_;
        continue;
      }
      break;
    }
  }

  void skipLongBracketEnd(int eqCount) {
    while (!eof()) {
      if (peek() == ']') {
        std::size_t scan = pos_ + 1;
        int matched = 0;
        while (matched < eqCount && scan < src_.size() && src_[scan] == '=') {
          ++matched;
          ++scan;
        }
        if (matched == eqCount && scan < src_.size() && src_[scan] == ']') {
          pos_ = scan + 1;
          return;
        }
      }
      ++pos_;
    }
  }

  bool matchKeyword(std::string_view kw) {
    if (pos_ + kw.size() > src_.size())
      return false;
    if (src_.substr(pos_, kw.size()) != kw)
      return false;
    if (pos_ + kw.size() < src_.size() && isIdentChar(src_[pos_ + kw.size()]))
      return false;
    pos_ += kw.size();
    return true;
  }

  std::string parseIdent() {
    skipTrivia();
    if (eof() || !isIdentStart(peek()))
      return {};
    const std::size_t start = pos_;
    while (!eof() && isIdentChar(peek()))
      ++pos_;
    return std::string(src_.substr(start, pos_ - start));
  }

  std::string parseLongString() {
    // Assumes peek() == '['
    std::size_t scan = pos_ + 1;
    int eqCount = 0;
    while (scan < src_.size() && src_[scan] == '=') {
      ++eqCount;
      ++scan;
    }
    if (scan >= src_.size() || src_[scan] != '[')
      return {};
    pos_ = scan + 1;
    if (peek() == '\r' && peek(1) == '\n')
      pos_ += 2;
    else if (peek() == '\n')
      ++pos_;

    const std::size_t start = pos_;
    while (!eof()) {
      if (peek() == ']') {
        std::size_t endScan = pos_ + 1;
        int matched = 0;
        while (matched < eqCount && endScan < src_.size() &&
               src_[endScan] == '=') {
          ++matched;
          ++endScan;
        }
        if (matched == eqCount && endScan < src_.size() &&
            src_[endScan] == ']') {
          std::string out(src_.substr(start, pos_ - start));
          pos_ = endScan + 1;
          return out;
        }
      }
      ++pos_;
    }
    return std::string(src_.substr(start));
  }

  std::string parseQuotedString() {
    const char quote = peek();
    ++pos_;
    std::string out;
    while (!eof()) {
      const char c = src_[pos_++];
      if (c == quote)
        break;
      if (c == '\\' && !eof()) {
        const char esc = src_[pos_++];
        switch (esc) {
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '"':
          out.push_back('"');
          break;
        case '\'':
          out.push_back('\'');
          break;
        case '0':
          out.push_back('\0');
          break;
        case 'x': {
          if (pos_ + 2 <= src_.size()) {
            char hex[3] = {src_[pos_], src_[pos_ + 1], '\0'};
            char *end = nullptr;
            const unsigned long v = std::strtoul(hex, &end, 16);
            if (end == hex + 2) {
              out.push_back(static_cast<char>(v));
              pos_ += 2;
            }
          }
          break;
        }
        case '\r':
          if (peek() == '\n')
            ++pos_;
          out.push_back('\n');
          break;
        case '\n':
          out.push_back('\n');
          break;
        default:
          if (std::isdigit(static_cast<unsigned char>(esc))) {
            int val = esc - '0';
            for (int d = 0; d < 2 && !eof() &&
                            std::isdigit(static_cast<unsigned char>(peek()));
                 ++d) {
              val = val * 10 + (src_[pos_++] - '0');
            }
            out.push_back(static_cast<char>(val & 0xFF));
          } else {
            out.push_back(esc);
          }
          break;
        }
      } else {
        out.push_back(c);
      }
    }
    return out;
  }

  std::string parseBracketKey() {
    // Assumes peek() == '['
    ++pos_;
    skipTrivia();
    std::string key;
    if (peek() == '"' || peek() == '\'') {
      key = parseQuotedString();
    } else {
      LuaValue v = parseValue();
      key = v.asString();
    }
    skipTrivia();
    if (peek() == ']')
      ++pos_;
    return key;
  }

  LuaValue parseNumber() {
    const std::size_t start = pos_;
    if (peek() == '-' || peek() == '+')
      ++pos_;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
      pos_ += 2;
      while (!eof() && std::isxdigit(static_cast<unsigned char>(peek())))
        ++pos_;
      const std::string token(src_.substr(start, pos_ - start));
      return LuaValue::makeNumber(
          static_cast<double>(std::strtoll(token.c_str(), nullptr, 16)));
    }
    while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
      ++pos_;
    if (peek() == '.') {
      ++pos_;
      while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
        ++pos_;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++pos_;
      if (peek() == '+' || peek() == '-')
        ++pos_;
      while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
        ++pos_;
    }
    const std::string token(src_.substr(start, pos_ - start));
    return LuaValue::makeNumber(std::strtod(token.c_str(), nullptr));
  }

  static void setField(LuaTable &table, std::string key, LuaValue val) {
    auto it = table.fieldIndex.find(key);
    if (it != table.fieldIndex.end()) {
      table.fields[it->second].second = std::move(val);
    } else {
      table.fieldIndex[key] = table.fields.size();
      table.fields.emplace_back(std::move(key), std::move(val));
    }
  }

  LuaValue parseTable() {
    auto table = std::make_shared<LuaTable>();
    ++pos_; // consume '{'
    while (true) {
      skipTrivia();
      if (eof())
        break;
      if (peek() == '}') {
        ++pos_;
        break;
      }

      if (peek() == '[') {
        // Check if long string [[...]] vs bracket key [expr] = val
        if (peek(1) == '[' || peek(1) == '=') {
          const std::size_t save = pos_;
          std::string ls = parseLongString();
          if (pos_ > save) {
            table->array.push_back(LuaValue::makeString(std::move(ls)));
          } else {
            ++pos_;
          }
        } else {
          std::string key = parseBracketKey();
          skipTrivia();
          if (peek() == '=') {
            ++pos_;
            LuaValue val = parseValue();
            setField(*table, std::move(key), std::move(val));
          }
        }
      } else if (isIdentStart(peek())) {
        const std::size_t save = pos_;
        std::string ident = parseIdent();
        skipTrivia();
        if (peek() == '=') {
          ++pos_;
          LuaValue val = parseValue();
          setField(*table, std::move(ident), std::move(val));
        } else {
          // Not a key=value; rewind and parse as value (true/false/nil/ident).
          pos_ = save;
          table->array.push_back(parseValue());
        }
      } else {
        table->array.push_back(parseValue());
      }

      skipTrivia();
      if (peek() == ',' || peek() == ';')
        ++pos_;
    }
    return LuaValue::makeTable(std::move(table));
  }

  LuaValue parseValue() {
    skipTrivia();
    if (eof())
      return LuaValue::makeNil();

    const char c = peek();
    if (c == '{')
      return parseTable();
    if (c == '"' || c == '\'')
      return LuaValue::makeString(parseQuotedString());
    if (c == '[' && (peek(1) == '[' || peek(1) == '='))
      return LuaValue::makeString(parseLongString());
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        ((c == '-' || c == '+') &&
         std::isdigit(static_cast<unsigned char>(peek(1))))) {
      return parseNumber();
    }
    if (matchKeyword("true"))
      return LuaValue::makeBool(true);
    if (matchKeyword("false"))
      return LuaValue::makeBool(false);
    if (matchKeyword("nil"))
      return LuaValue::makeNil();
    if (isIdentStart(c)) {
      return LuaValue::makeString(parseIdent());
    }

    // Fallback: advance one byte to avoid infinite loop.
    ++pos_;
    return LuaValue::makeNil();
  }

  std::string_view src_;
  std::size_t pos_ = 0;
};

} // namespace

std::shared_ptr<LuaTable> AstParser::parseTopLevel(std::string_view source) {
  LuaTableReader reader(source);
  return reader.parseTopLevel();
}

ParsedAstScript AstParser::parseScript(std::string_view source) {
  ParsedAstScript result;
  result.root = parseTopLevel(source);
  if (!result.root)
    return result;

  result.astver = result.root->getNumber("astver", 2.0);
  if (const auto *astVal = result.root->get("ast");
      astVal && astVal->isTable()) {
    result.ast = astVal->tableVal;
    for (const auto &[key, val] : result.ast->fields) {
      if (key == "label") {
        if (val.isTable())
          result.label = val.tableVal;
      } else if (val.isTable()) {
        result.blockOrder.push_back(key);
      }
    }
  }
  return result;
}

IptSpec AstParser::parseIpt(std::string_view source) {
  IptSpec spec;
  auto root = parseTopLevel(source);
  if (!root)
    return spec;
  const auto *iptVal = root->get("ipt");
  if (!iptVal || !iptVal->isTable())
    return spec;
  const LuaTable &tbl = *iptVal->tableVal;
  spec.mode = tbl.getString("mode");
  if (const LuaTable *base = tbl.getTable("base")) {
    spec.x = base->getInt("x", 0);
    spec.y = base->getInt("y", 0);
    spec.w = base->getInt("w", 0);
    spec.h = base->getInt("h", 0);
    spec.ax = base->getInt("ax", 0);
    spec.ay = base->getInt("ay", 0);
    spec.frameTimeMs = base->getNumber("time", 33.3);
    spec.loop = base->getInt("loop", 0);
  }
  for (const auto &item : tbl.array) {
    if (item.isTable() && !item.tableVal->array.empty()) {
      spec.frames.push_back(item.tableVal->array[0].asString());
    } else if (item.isString()) {
      spec.frames.push_back(item.strVal);
    }
  }
  return spec;
}

} // namespace Artemis
