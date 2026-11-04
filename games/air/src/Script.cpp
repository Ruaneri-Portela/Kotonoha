// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "Air/Script.hpp"
#include <stdexcept>
namespace Air {
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
    throw std::runtime_error("Missing AIR scenario " + std::to_string(n));
  return *s;
}
std::size_t ScriptArchive::ValidateAll() {
  std::size_t count = 0;
  for (int n : ScenarioNumbers())
    for (const auto &e : Scenario(n)) {
      if (!e)
        throw std::runtime_error("Null AIR bytecode element");
      ++count;
    }
  return count;
}
libreallive::ExpressionPiece Parameter(const libreallive::CommandElement &c,
                                       std::size_t i) {
  if (i >= c.GetParamCount())
    throw std::out_of_range("AIR command parameter");
  auto raw = c.GetParam(static_cast<int>(i));
  const char *p = raw.c_str();
  return libreallive::GetData(p);
}
int IntegerParameter(RLMachine &m, const libreallive::CommandElement &c,
                     std::size_t i) {
  return Parameter(c, i).GetIntegerValue(m);
}
std::string StringParameter(RLMachine &m, const libreallive::CommandElement &c,
                            std::size_t i) {
  return Parameter(c, i).GetStringValue(m);
}
} // namespace Air
