// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Artemis {

struct LuaValue;
struct LuaTable;

struct LuaTable {
  // Positional (array) elements in order (1-based in Lua, 0-based here).
  std::vector<LuaValue> array;
  // Named / keyed entries preserving insertion order (critical for AST blocks).
  std::vector<std::pair<std::string, LuaValue>> fields;
  std::unordered_map<std::string, std::size_t> fieldIndex;

  const LuaValue *get(std::string_view key) const;
  std::string getString(std::string_view key,
                        std::string_view fallback = {}) const;
  double getNumber(std::string_view key, double fallback = 0.0) const;
  int getInt(std::string_view key, int fallback = 0) const;
  bool getBool(std::string_view key, bool fallback = false) const;
  const LuaTable *getTable(std::string_view key) const;
  bool has(std::string_view key) const;
};

struct LuaValue {
  enum class Type { Nil, Bool, Number, String, Table };

  Type type = Type::Nil;
  bool boolVal = false;
  double numVal = 0.0;
  std::string strVal;
  std::shared_ptr<LuaTable> tableVal;

  static LuaValue makeNil() { return LuaValue{}; }
  static LuaValue makeBool(bool b) {
    LuaValue v;
    v.type = Type::Bool;
    v.boolVal = b;
    return v;
  }
  static LuaValue makeNumber(double n) {
    LuaValue v;
    v.type = Type::Number;
    v.numVal = n;
    return v;
  }
  static LuaValue makeString(std::string s) {
    LuaValue v;
    v.type = Type::String;
    v.strVal = std::move(s);
    return v;
  }
  static LuaValue makeTable(std::shared_ptr<LuaTable> t = nullptr) {
    LuaValue v;
    v.type = Type::Table;
    v.tableVal = t ? std::move(t) : std::make_shared<LuaTable>();
    return v;
  }

  bool isNil() const { return type == Type::Nil; }
  bool isBool() const { return type == Type::Bool; }
  bool isNumber() const { return type == Type::Number; }
  bool isString() const { return type == Type::String; }
  bool isTable() const { return type == Type::Table && tableVal != nullptr; }

  std::string asString(std::string_view fallback = {}) const;
  double asNumber(double fallback = 0.0) const;
  int asInt(int fallback = 0) const;
  bool asBool(bool fallback = false) const;
};

struct ParsedAstScript {
  double astver = 2.0;
  std::shared_ptr<LuaTable> root;  // Top-level assignments (astver, ast, etc.)
  std::shared_ptr<LuaTable> ast;   // `ast = { ... }` table
  std::shared_ptr<LuaTable> label; // `ast.label` table (optional)
  std::vector<std::string> blockOrder;
};

struct IptSpec {
  std::string mode;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int ax = 0;
  int ay = 0;
  double frameTimeMs = 33.3;
  int loop = 0; // -1 infinite, 0 once
  std::vector<std::string> frames;
};

// Pure C++17 recursive-descent parser for Artemis Engine .ast and .ipt Lua
// table files (zero external Lua runtime required).
class AstParser {
public:
  static ParsedAstScript parseScript(std::string_view source);
  static IptSpec parseIpt(std::string_view source);
  static std::shared_ptr<LuaTable> parseTopLevel(std::string_view source);
};

} // namespace Artemis
