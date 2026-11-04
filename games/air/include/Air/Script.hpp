// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include "libreallive/archive.h"
#include "machine/rlmachine.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>
namespace Air {
class ScriptArchive {
  std::unique_ptr<libreallive::Archive> archive_;

public:
  explicit ScriptArchive(const std::filesystem::path &seen);
  ScriptArchive(const std::vector<std::uint8_t> &bytes,
                const std::map<int, std::vector<std::uint8_t>> &overrides = {});
  std::vector<int> ScenarioNumbers() const;
  libreallive::Scenario &Scenario(int number);
  std::size_t ValidateAll();
};
// Parse a raw command argument, preserving memory references for assignments.
libreallive::ExpressionPiece Parameter(const libreallive::CommandElement &,
                                       std::size_t index);
int IntegerParameter(RLMachine &, const libreallive::CommandElement &,
                     std::size_t index);
std::string StringParameter(RLMachine &, const libreallive::CommandElement &,
                            std::size_t index);
} // namespace Air
