// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/VariableStore.hpp>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace N2System {

std::string VariableStore::normalizeName(std::string_view name) {
  std::string res(name);
  while (!res.empty() && (res.front() == ' ' || res.front() == '\t'))
    res.erase(res.begin());
  while (!res.empty() && (res.back() == ' ' || res.back() == '\t'))
    res.pop_back();
  return res;
}

void VariableStore::setInt(std::string_view name, int64_t value) {
  const auto norm = normalizeName(name);
  if (!norm.empty())
    integers_[norm] = value;
}

int64_t VariableStore::getInt(std::string_view name,
                              int64_t defaultValue) const {
  const auto norm = normalizeName(name);
  const auto it = integers_.find(norm);
  if (it != integers_.end())
    return it->second;
  return defaultValue;
}

void VariableStore::setString(std::string_view name, std::string_view value) {
  const auto norm = normalizeName(name);
  if (!norm.empty())
    strings_[norm] = std::string(value);
}

std::string VariableStore::getString(std::string_view name,
                                     std::string_view defaultValue) const {
  const auto norm = normalizeName(name);
  const auto it = strings_.find(norm);
  if (it != strings_.end())
    return it->second;
  return std::string(defaultValue);
}

void VariableStore::setFlag(std::string_view name, bool value) {
  const auto norm = normalizeName(name);
  if (!norm.empty())
    flags_[norm] = value;
}

bool VariableStore::getFlag(std::string_view name, bool defaultValue) const {
  const auto norm = normalizeName(name);
  const auto it = flags_.find(norm);
  if (it != flags_.end())
    return it->second;
  return defaultValue;
}

void VariableStore::set(std::string_view name, const VarValue &value) {
  const auto norm = normalizeName(name);
  if (norm.empty())
    return;
  if (!norm.empty() && norm.front() == '@') {
    if (auto p = std::get_if<std::string>(&value))
      strings_[norm] = *p;
    else if (auto pi = std::get_if<int64_t>(&value))
      strings_[norm] = std::to_string(*pi);
  } else if (!norm.empty() && norm.front() == '#') {
    if (auto pb = std::get_if<bool>(&value))
      flags_[norm] = *pb;
    else if (auto pi = std::get_if<int64_t>(&value))
      flags_[norm] = (*pi != 0);
  } else {
    if (auto pi = std::get_if<int64_t>(&value))
      integers_[norm] = *pi;
    else if (auto pb = std::get_if<bool>(&value))
      integers_[norm] = *pb ? 1 : 0;
  }
}

VarValue VariableStore::get(std::string_view name) const {
  const auto norm = normalizeName(name);
  if (!norm.empty() && norm.front() == '@')
    return getString(norm);
  if (!norm.empty() && norm.front() == '#')
    return getFlag(norm);
  return getInt(norm);
}

bool VariableStore::has(std::string_view name) const {
  const auto norm = normalizeName(name);
  return integers_.count(norm) || strings_.count(norm) || flags_.count(norm);
}

void VariableStore::clear() {
  integers_.clear();
  strings_.clear();
  flags_.clear();
}

VarValue VariableStore::evaluate(std::string_view expr) const {
  std::string s(expr);
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    s.pop_back();

  if (s.empty())
    return int64_t(0);

  // String literal
  if ((s.front() == '"' && s.back() == '"') ||
      (s.front() == '\'' && s.back() == '\'')) {
    if (s.size() >= 2)
      return s.substr(1, s.size() - 2);
    return std::string();
  }

  // Boolean literal
  if (s == "true" || s == "TRUE")
    return true;
  if (s == "false" || s == "FALSE")
    return false;

  // Check equality / comparison operators
  auto findOp = [&](std::string_view op) -> std::size_t {
    bool inQuote = false;
    char quoteChar = 0;
    int parenDepth = 0;
    for (std::size_t i = 0; i + op.size() <= s.size(); ++i) {
      char c = s[i];
      if (inQuote) {
        if (c == quoteChar)
          inQuote = false;
        continue;
      }
      if (c == '"' || c == '\'') {
        inQuote = true;
        quoteChar = c;
        continue;
      }
      if (c == '(')
        parenDepth++;
      else if (c == ')')
        parenDepth--;
      if (parenDepth == 0 && s.substr(i, op.size()) == op)
        return i;
    }
    return std::string::npos;
  };

  // Logical ||
  if (auto pos = findOp("||"); pos != std::string::npos) {
    return evaluateBool(s.substr(0, pos)) || evaluateBool(s.substr(pos + 2));
  }
  // Logical &&
  if (auto pos = findOp("&&"); pos != std::string::npos) {
    return evaluateBool(s.substr(0, pos)) && evaluateBool(s.substr(pos + 2));
  }
  // Equality ==
  if (auto pos = findOp("=="); pos != std::string::npos) {
    auto left = evaluate(s.substr(0, pos));
    auto right = evaluate(s.substr(pos + 2));
    return left == right;
  }
  // Inequality !=
  if (auto pos = findOp("!="); pos != std::string::npos) {
    auto left = evaluate(s.substr(0, pos));
    auto right = evaluate(s.substr(pos + 2));
    return left != right;
  }
  // Less or equal <=
  if (auto pos = findOp("<="); pos != std::string::npos) {
    return evaluateInt(s.substr(0, pos)) <= evaluateInt(s.substr(pos + 2));
  }
  // Greater or equal >=
  if (auto pos = findOp(">="); pos != std::string::npos) {
    return evaluateInt(s.substr(0, pos)) >= evaluateInt(s.substr(pos + 2));
  }
  // Less <
  if (auto pos = findOp("<"); pos != std::string::npos) {
    return evaluateInt(s.substr(0, pos)) < evaluateInt(s.substr(pos + 1));
  }
  // Greater >
  if (auto pos = findOp(">"); pos != std::string::npos) {
    return evaluateInt(s.substr(0, pos)) > evaluateInt(s.substr(pos + 1));
  }

  // Addition / Subtraction
  if (auto pos = findOp("+"); pos != std::string::npos && pos > 0) {
    auto left = evaluate(s.substr(0, pos));
    auto right = evaluate(s.substr(pos + 1));
    if (std::holds_alternative<std::string>(left) ||
        std::holds_alternative<std::string>(right))
      return evaluateString(s.substr(0, pos)) +
             evaluateString(s.substr(pos + 1));
    return evaluateInt(s.substr(0, pos)) + evaluateInt(s.substr(pos + 1));
  }
  if (auto pos = findOp("-"); pos != std::string::npos && pos > 0) {
    return evaluateInt(s.substr(0, pos)) - evaluateInt(s.substr(pos + 1));
  }

  // Multiplication / Division / Modulo
  if (auto pos = findOp("*"); pos != std::string::npos) {
    return evaluateInt(s.substr(0, pos)) * evaluateInt(s.substr(pos + 1));
  }
  if (auto pos = findOp("/"); pos != std::string::npos) {
    auto denom = evaluateInt(s.substr(pos + 1));
    return denom == 0 ? 0 : evaluateInt(s.substr(0, pos)) / denom;
  }

  // Logical NOT !
  if (s.front() == '!') {
    return !evaluateBool(s.substr(1));
  }

  // Parentheses
  if (s.front() == '(' && s.back() == ')') {
    return evaluate(s.substr(1, s.size() - 2));
  }

  // Numeric literal
  bool isNum = true;
  std::size_t startIdx = 0;
  if (s[0] == '-' || s[0] == '+')
    startIdx = 1;
  if (startIdx >= s.size())
    isNum = false;
  for (std::size_t i = startIdx; i < s.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
      isNum = false;
      break;
    }
  }
  if (isNum) {
    try {
      return std::stoll(s);
    } catch (...) {
      return int64_t(0);
    }
  }

  // Variable lookup (NsScript variables start with $, @, or #)
  if (!s.empty() && (s[0] == '$' || s[0] == '@' || s[0] == '#'))
    return get(s);
  if (integers_.count(s) || strings_.count(s) || flags_.count(s))
    return get(s);
  return std::string(s);
}

bool VariableStore::evaluateBool(std::string_view expression) const {
  auto val = evaluate(expression);
  if (auto pb = std::get_if<bool>(&val))
    return *pb;
  if (auto pi = std::get_if<int64_t>(&val))
    return *pi != 0;
  if (auto ps = std::get_if<std::string>(&val))
    return !ps->empty() && *ps != "false" && *ps != "FALSE" && *ps != "0";
  return false;
}

int64_t VariableStore::evaluateInt(std::string_view expression) const {
  auto val = evaluate(expression);
  if (auto pi = std::get_if<int64_t>(&val))
    return *pi;
  if (auto pb = std::get_if<bool>(&val))
    return *pb ? 1 : 0;
  if (auto ps = std::get_if<std::string>(&val)) {
    try {
      return std::stoll(*ps);
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

std::string VariableStore::evaluateString(std::string_view expression) const {
  auto val = evaluate(expression);
  if (auto ps = std::get_if<std::string>(&val))
    return *ps;
  if (auto pi = std::get_if<int64_t>(&val))
    return std::to_string(*pi);
  if (auto pb = std::get_if<bool>(&val))
    return *pb ? "true" : "false";
  return std::string();
}

} // namespace N2System
