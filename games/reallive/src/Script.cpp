// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLive/Script.hpp"
#include <stdexcept>
namespace RealLive {
ScriptArchive::ScriptArchive(const std::filesystem::path &seen)
    : archive_(std::make_unique<libreallive::Archive>(seen.string())) {}
ScriptArchive::ScriptArchive(
    const std::vector<std::uint8_t> &bytes,
    const std::map<int, std::vector<std::uint8_t>> &overrides) {
  std::map<int, std::vector<char>> scripts;
  for (const auto &[id, data] : overrides)
    scripts.emplace(id, std::vector<char>(data.begin(), data.end()));
  archive_ = std::make_unique<libreallive::Archive>(
      std::vector<char>(bytes.begin(), bytes.end()), std::move(scripts));
}
std::vector<int> ScriptArchive::ScenarioNumbers() const {
  std::vector<int> result;
  for (auto it = archive_->begin(); it != archive_->end(); ++it)
    result.push_back(it->first);
  return result;
}
libreallive::Scenario &ScriptArchive::Scenario(int n) {
  auto *s = archive_->GetScenario(n);
  if (!s)
    throw std::runtime_error("Missing REALLIVE scenario " + std::to_string(n));
  return *s;
}
std::size_t ScriptArchive::ValidateAll() {
  std::size_t count = 0;
  for (int n : ScenarioNumbers())
    for (const auto &e : Scenario(n)) {
      if (!e)
        throw std::runtime_error("Null REALLIVE bytecode element");
      ++count;
    }
  return count;
}
libreallive::ExpressionPiece Parameter(const libreallive::CommandElement &c,
                                       std::size_t i) {
  if (i >= c.GetParamCount()) {
    throw std::out_of_range(
        "REALLIVE command parameter index " + std::to_string(i) +
        " out of range (max: " + std::to_string(c.GetParamCount()) + ")");
  }
  auto raw = c.GetParam(static_cast<int>(i));
  const char *p = raw.c_str();
  try {
    // Command parameters can contain an untagged complex tuple. A normal
    // scalar can use the same bytecode wrapper, e.g. `(310)`. Parse the
    // wrapper structurally: one contained expression is a scalar, while two
    // or more remain a complex range (used by sums).
    if (*p == '(') {
      auto complex = libreallive::GetComplexParam(p);
      const auto &parts = complex.GetContainedPieces();
      if (parts.size() == 1)
        return parts.front();
      return complex;
    }
    return libreallive::GetData(p);
  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to parse parameter " + std::to_string(i) +
                             ": " + std::string(e.what()));
  }
}
int IntegerParameter(RLMachine &m, const libreallive::CommandElement &c,
                     std::size_t i) {
  try {
    auto piece = Parameter(c, i);
    return piece.GetIntegerValue(m);
  } catch (const std::exception &e) {
    throw std::runtime_error("Cannot get integer value from parameter " +
                             std::to_string(i) + ": " + std::string(e.what()));
  }
}
std::string StringParameter(RLMachine &m, const libreallive::CommandElement &c,
                            std::size_t i) {
  try {
    auto piece = Parameter(c, i);
    return piece.GetStringValue(m);
  } catch (const std::exception &e) {
    throw std::runtime_error("Cannot get string value from parameter " +
                             std::to_string(i) + ": " + std::string(e.what()));
  }
}
} // namespace RealLive
