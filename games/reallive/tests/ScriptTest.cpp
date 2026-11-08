// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLive/Script.hpp"
#include <cassert>
#include <iostream>
int main(int argc, char **argv) {
  RLMachine machine;
  machine.SetIntValue(libreallive::IntMemRef(0, 0), 0x12345678);
  machine.SetIntValue(libreallive::IntMemRef(0, 4, 1), 0xab);
  assert(static_cast<unsigned>(
             machine.GetIntValue(libreallive::IntMemRef(0, 0))) == 0x1234ab78u);
  machine.SetIntValue(libreallive::IntMemRef(0, 1, 0), 1);
  assert(machine.GetIntValue(libreallive::IntMemRef(0, 1, 0)) == 1);
  const char *expr = "$\xff\x2a\0\0\0";
  assert(libreallive::GetExpression(expr).GetIntegerValue(machine) == 42);
  RealLive::ScriptArchive empty(std::vector<std::uint8_t>(80000));
  assert(empty.ScenarioNumbers().empty());
  bool rejected = false;
  try {
    RealLive::ScriptArchive truncated(std::vector<std::uint8_t>(79999));
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  rejected = false;
  try {
    RealLive::ScriptArchive invalidOverride(std::vector<std::uint8_t>(80000),
                                            {{10000, {}}});
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  if (argc > 1) {
    RealLive::ScriptArchive a(argv[1]);
    std::cout << "scenarios=" << a.ScenarioNumbers().size()
              << " elements=" << a.ValidateAll()
              << " encoding=" << a.Scenario(9030).encoding() << '\n';
  }
}
