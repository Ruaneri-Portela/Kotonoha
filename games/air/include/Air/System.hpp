// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include "Air/Script.hpp"
#include <functional>
namespace Air {
// Pure system state uses integer bank26 and string banks26(global names),
// 27(local names),28(edit text). Session snapshots preserve these banks.
bool systemCommand(RLMachine &, const libreallive::CommandElement &,
                   const std::function<int(const std::string &, int)> &config);
std::string systemName(RLMachine &, bool global, int index);
} // namespace Air