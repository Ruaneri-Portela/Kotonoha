// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace N2System {

using VarValue = std::variant<int64_t, std::string, bool>;

class VariableStore {
public:
  VariableStore() = default;

  void setInt(std::string_view name, int64_t value);
  int64_t getInt(std::string_view name, int64_t defaultValue = 0) const;

  void setString(std::string_view name, std::string_view value);
  std::string getString(std::string_view name, std::string_view defaultValue = "") const;

  void setFlag(std::string_view name, bool value);
  bool getFlag(std::string_view name, bool defaultValue = false) const;

  void set(std::string_view name, const VarValue &value);
  VarValue get(std::string_view name) const;
  bool has(std::string_view name) const;

  // Evaluates a simple expression containing literals, variables, arithmetic,
  // comparison, and logical operators. Returns the calculated VarValue.
  VarValue evaluate(std::string_view expression) const;
  bool evaluateBool(std::string_view expression) const;
  int64_t evaluateInt(std::string_view expression) const;
  std::string evaluateString(std::string_view expression) const;

  const std::map<std::string, int64_t> &integers() const { return integers_; }
  const std::map<std::string, std::string> &strings() const { return strings_; }
  const std::map<std::string, bool> &flags() const { return flags_; }

  void clear();

  static std::string normalizeName(std::string_view name);

private:
  std::map<std::string, int64_t> integers_;
  std::map<std::string, std::string> strings_;
  std::map<std::string, bool> flags_;
};

} // namespace N2System
