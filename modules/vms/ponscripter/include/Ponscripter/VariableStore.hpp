// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Ponscripter/PonscripterTypes.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ponscripter {

class VariableStore {
public:
  static constexpr int kTotalVariables = 4096;
  static constexpr int kDefaultGlobalBorder = 200;

  VariableStore();

  void resetAll();
  // Resets only local variables (0 .. globalBorder - 1), preserving globals.
  void resetLocals();

  void setGlobalOn(bool enabled) { globalOn_ = enabled; }
  bool isGlobalOn() const { return globalOn_; }

  void setGlobalBorder(int border);
  int globalBorder() const { return globalBorder_; }

  // Numeric aliases (numalias name, number)
  void setNumAlias(std::string_view name, int value);
  bool findNumAlias(std::string_view name, int &outValue) const;

  // String aliases (stralias name, "string")
  void setStrAlias(std::string_view name, std::string_view value);
  bool findStrAlias(std::string_view name, std::string &outValue) const;

  // Numeric variables %0 .. %4095
  int getNum(int id) const;
  int getNum(std::string_view aliasOrId) const;
  // Returns true if a global variable (>= globalBorder) was modified.
  bool setNum(int id, int value);
  bool setNum(std::string_view aliasOrId, int value);

  // Resolves a variable reference after '%', e.g. "111", "volmp3", or "%0"
  int resolveVarId(std::string_view token) const;

  // String variables $0 .. $4095
  const std::string &getStr(int id) const;
  void setStr(int id, std::string value);

  // Arrays ?0 .. ?N
  void dimArray(int arrayId, const std::vector<int> &dimensions);
  int getArray(int arrayId, const std::vector<int> &indices) const;
  // Number of writable cells in the final dimension, starting at indices.
  int remainingArrayElements(int arrayId, const std::vector<int> &indices) const;
  void setArray(int arrayId, const std::vector<int> &indices, int value);

  // Global variable snapshot for gloval.sav
  std::unordered_map<int, int> exportGlobals() const;
  void importGlobals(const std::unordered_map<int, int> &globals);

  // Local variable snapshot for save1..20.dat
  std::unordered_map<int, int> exportLocals() const;
  void importLocals(const std::unordered_map<int, int> &locals);

  static std::string normalizeKey(std::string_view raw);

private:
  bool globalOn_ = false;
  int globalBorder_ = kDefaultGlobalBorder;
  std::vector<int> numVars_;
  std::vector<std::string> strVars_;
  std::unordered_map<std::string, int> numAliases_;
  std::unordered_map<std::string, std::string> strAliases_;

  struct ArrayData {
    std::vector<int> dims;
    std::vector<int> values;
  };
  std::unordered_map<int, ArrayData> arrays_;
  std::string emptyStr_;
};

} // namespace Ponscripter
