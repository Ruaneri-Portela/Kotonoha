// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Ponscripter/VariableStore.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Ponscripter {

VariableStore::VariableStore()
    : numVars_(kTotalVariables, 0), strVars_(kTotalVariables) {}

void VariableStore::resetAll() {
  std::fill(numVars_.begin(), numVars_.end(), 0);
  for (auto &s : strVars_)
    s.clear();
  numAliases_.clear();
  strAliases_.clear();
  arrays_.clear();
  globalOn_ = false;
  globalBorder_ = kDefaultGlobalBorder;
}

void VariableStore::resetLocals() {
  const int limit = std::clamp(globalBorder_, 0, kTotalVariables);
  for (int i = 0; i < limit; ++i)
    numVars_[i] = 0;
  for (auto &s : strVars_)
    s.clear();
  for (auto &[id, arr] : arrays_)
    std::fill(arr.values.begin(), arr.values.end(), 0);
}

void VariableStore::setGlobalBorder(int border) {
  globalBorder_ = std::clamp(border, 0, kTotalVariables);
}

std::string VariableStore::normalizeKey(std::string_view raw) {
  std::size_t start = 0;
  while (start < raw.size() &&
         std::isspace(static_cast<unsigned char>(raw[start])))
    ++start;
  std::size_t end = raw.size();
  while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1])))
    --end;
  std::string out(raw.substr(start, end - start));
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

void VariableStore::setNumAlias(std::string_view name, int value) {
  const std::string key = normalizeKey(name);
  if (!key.empty())
    numAliases_[key] = value;
}

bool VariableStore::findNumAlias(std::string_view name, int &outValue) const {
  const std::string key = normalizeKey(name);
  const auto it = numAliases_.find(key);
  if (it == numAliases_.end())
    return false;
  outValue = it->second;
  return true;
}

void VariableStore::setStrAlias(std::string_view name, std::string_view value) {
  const std::string key = normalizeKey(name);
  if (!key.empty())
    strAliases_[key] = std::string(value);
}

bool VariableStore::findStrAlias(std::string_view name,
                                 std::string &outValue) const {
  const std::string key = normalizeKey(name);
  const auto it = strAliases_.find(key);
  if (it == strAliases_.end())
    return false;
  outValue = it->second;
  return true;
}

int VariableStore::resolveVarId(std::string_view token) const {
  while (!token.empty() &&
         std::isspace(static_cast<unsigned char>(token.front())))
    token.remove_prefix(1);
  while (!token.empty() &&
         std::isspace(static_cast<unsigned char>(token.back())))
    token.remove_suffix(1);
  if (token.empty())
    return -1;

  int indirection = 0;
  while (!token.empty() && token.front() == '%') {
    ++indirection;
    token.remove_prefix(1);
  }
  if (token.empty())
    return -1;

  int baseId = -1;
  if (std::isdigit(static_cast<unsigned char>(token.front()))) {
    auto [p, ec] =
        std::from_chars(token.data(), token.data() + token.size(), baseId);
    (void)p;
    if (ec != std::errc{})
      return -1;
  } else {
    if (!findNumAlias(token, baseId))
      return -1;
  }

  // Handle indirect variable access e.g. `%%0` -> value of `%0` is var ID
  for (int i = 1; i < indirection; ++i) {
    baseId = getNum(baseId);
  }
  return baseId;
}

int VariableStore::getNum(int id) const {
  if (id < 0 || id >= kTotalVariables)
    return 0;
  return numVars_[id];
}

int VariableStore::getNum(std::string_view aliasOrId) const {
  const int id = resolveVarId(aliasOrId);
  return getNum(id);
}

bool VariableStore::setNum(int id, int value) {
  if (id < 0 || id >= kTotalVariables)
    return false;
  const bool changed = (numVars_[id] != value);
  numVars_[id] = value;
  return changed && globalOn_ && (id >= globalBorder_);
}

bool VariableStore::setNum(std::string_view aliasOrId, int value) {
  const int id = resolveVarId(aliasOrId);
  return setNum(id, value);
}

const std::string &VariableStore::getStr(int id) const {
  if (id < 0 || id >= kTotalVariables)
    return emptyStr_;
  return strVars_[id];
}

void VariableStore::setStr(int id, std::string value) {
  if (id >= 0 && id < kTotalVariables)
    strVars_[id] = std::move(value);
}

void VariableStore::dimArray(int arrayId, const std::vector<int> &dimensions) {
  if (dimensions.empty())
    return;
  ArrayData data;
  data.dims = dimensions;
  std::size_t total = 1;
  for (int d : dimensions) {
    total *= static_cast<std::size_t>(std::max(1, d + 1));
  }
  data.values.assign(std::min<std::size_t>(total, 1000000), 0);
  arrays_[arrayId] = std::move(data);
}

int VariableStore::remainingArrayElements(
    int arrayId, const std::vector<int> &indices) const {
  const auto it = arrays_.find(arrayId);
  if (it == arrays_.end() || indices.empty() ||
      indices.size() != it->second.dims.size() || it->second.values.empty())
    return 0;

  const auto &array = it->second;
  std::size_t offset = 0;
  std::size_t stride = 1;
  for (std::size_t i = 0; i < indices.size(); ++i) {
    const int index = indices[i];
    if (index < 0 || index > std::max(0, array.dims[i]))
      return 0;
    const std::size_t position = static_cast<std::size_t>(index);
    if (offset >= array.values.size() ||
        position > (array.values.size() - 1 - offset) / stride)
      return 0;
    offset += position * stride;
    if (i + 1 < indices.size()) {
      const std::size_t extent =
          static_cast<std::size_t>(std::max(0, array.dims[i])) + 1;
      if (stride > array.values.size() / extent)
        return 0;
      stride *= extent;
    }
  }

  const std::size_t remainingDimension =
      static_cast<std::size_t>(array.dims.back() - indices.back()) + 1;
  const std::size_t remainingStorage =
      (array.values.size() - 1 - offset) / stride + 1;
  return static_cast<int>(std::min(remainingDimension, remainingStorage));
}

int VariableStore::getArray(int arrayId,
                            const std::vector<int> &indices) const {
  const auto it = arrays_.find(arrayId);
  if (it == arrays_.end() || indices.empty())
    return 0;
  const auto &arr = it->second;
  std::size_t flat = 0;
  std::size_t stride = 1;
  for (std::size_t i = 0; i < indices.size() && i < arr.dims.size(); ++i) {
    const int idx = std::max(0, indices[i]);
    flat += static_cast<std::size_t>(idx) * stride;
    stride *= static_cast<std::size_t>(std::max(1, arr.dims[i] + 1));
  }
  if (flat >= arr.values.size())
    return 0;
  return arr.values[flat];
}

void VariableStore::setArray(int arrayId, const std::vector<int> &indices,
                             int value) {
  auto it = arrays_.find(arrayId);
  if (it == arrays_.end() || indices.empty())
    return;
  auto &arr = it->second;
  std::size_t flat = 0;
  std::size_t stride = 1;
  for (std::size_t i = 0; i < indices.size() && i < arr.dims.size(); ++i) {
    const int idx = std::max(0, indices[i]);
    flat += static_cast<std::size_t>(idx) * stride;
    stride *= static_cast<std::size_t>(std::max(1, arr.dims[i] + 1));
  }
  if (flat < arr.values.size())
    arr.values[flat] = value;
}

std::unordered_map<int, int> VariableStore::exportGlobals() const {
  std::unordered_map<int, int> out;
  for (int i = globalBorder_; i < kTotalVariables; ++i) {
    if (numVars_[i] != 0)
      out[i] = numVars_[i];
  }
  return out;
}

void VariableStore::importGlobals(const std::unordered_map<int, int> &globals) {
  for (int i = globalBorder_; i < kTotalVariables; ++i)
    numVars_[i] = 0;
  for (const auto &[id, val] : globals) {
    if (id >= globalBorder_ && id < kTotalVariables)
      numVars_[id] = val;
  }
}

std::unordered_map<int, int> VariableStore::exportLocals() const {
  std::unordered_map<int, int> out;
  const int limit = std::clamp(globalBorder_, 0, kTotalVariables);
  for (int i = 0; i < limit; ++i) {
    if (numVars_[i] != 0)
      out[i] = numVars_[i];
  }
  return out;
}

void VariableStore::importLocals(const std::unordered_map<int, int> &locals) {
  const int limit = std::clamp(globalBorder_, 0, kTotalVariables);
  for (int i = 0; i < limit; ++i)
    numVars_[i] = 0;
  for (const auto &[id, val] : locals) {
    if (id >= 0 && id < limit)
      numVars_[id] = val;
  }
}

} // namespace Ponscripter
