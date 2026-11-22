// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLive/Script.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
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
  // Construct a metadata-only RealLive scenario with the newer compiler
  // flag. The synthetic zero-length bytecode does not contain game data.
  // The old VFS constructor lost #REGNAME and rejected this scenario as
  // "Unknown script XOR key" before it could decode the header.
  std::vector<std::uint8_t> fullVoice(80000u + 0x1d0u + 9u, 0);
  const auto set32 = [&](std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i != 4; ++i)
      fullVoice.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
  };
  set32(0, 80000u);              // first scenario's archive offset
  set32(4, 0x1d0u + 9u);         // first scenario's length
  set32(80000u, 0x1d0u);         // RealLive header signature
  set32(80004u, 110002u);        // compiler with per-game second XOR
  set32(80000u + 0x20u, 0x1d0u); // compressed data offset
  set32(80000u + 0x24u, 0);      // empty synthetic bytecode
  set32(80000u + 0x28u, 9);      // minimal compressed header

  // Plain variants must not silently use the Full Voice format key.
  rejected = false;
  try {
    RealLive::ScriptArchive plain(fullVoice);
    (void)plain.Scenario(0);
  } catch (const std::exception &error) {
    rejected = std::string(error.what()).find("Unknown script XOR key") !=
               std::string::npos;
  }
  assert(rejected);
  RealLive::ScriptArchive voiced(fullVoice, {}, "KEY\\CLANNAD_FV");
  assert(voiced.ScenarioNumbers().size() == 1);
  assert(voiced.Scenario(0).encoding() == 0);

  // Malformed RealLive headers must be rejected before copying from an
  // unchecked scenario offset or attempting a huge allocation. Each case
  // contains only the synthetic, empty scenario constructed above.
  const auto write32 = [](std::vector<std::uint8_t> &bytes,
                          std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i)
      bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
  };
  const auto mustReject = [&](const std::vector<std::uint8_t> &bytes,
                              const char *description) {
    bool failed = false;
    try {
      RealLive::ScriptArchive malformed(bytes, {}, "KEY\\\\CLANNAD_FV");
      (void)malformed.Scenario(0);
    } catch (const std::exception &) {
      failed = true;
    }
    if (!failed)
      throw std::runtime_error(std::string("Unvalidated RealLive ") +
                               description);
  };
  {
    auto bad = fullVoice;
    write32(bad, 80000u + 0x28u, 99u);
    mustReject(bad, "compressed section extent");
  }
  {
    auto bad = fullVoice;
    write32(bad, 80000u + 0x08u, 0x7fffffffu);
    write32(bad, 80000u + 0x0cu, 1u);
    mustReject(bad, "kidoku table extent");
  }
  {
    auto bad = fullVoice;
    write32(bad, 80000u + 0x24u, 0xffffffffu);
    mustReject(bad, "negative decoded script size");
  }
  {
    auto bad = fullVoice;
    write32(bad, 80000u + 0x18u, 1u);
    write32(bad, 80000u + 0x14u, 0x1ceu);
    mustReject(bad, "character-name table extent");
  }
  {
    auto bad = fullVoice;
    write32(bad, 80000u + 0x14u, 0x1d0u);
    write32(bad, 80000u + 0x1cu, 99u);
    mustReject(bad, "metadata extent");
  }
  {
    auto bad = fullVoice;
    write32(bad, 80000u + 0x24u, 1u);
    mustReject(bad, "truncated compressed script payload");
  }

  if (argc > 1) {
    RealLive::ScriptArchive a(argv[1]);
    std::cout << "scenarios=" << a.ScenarioNumbers().size()
              << " elements=" << a.ValidateAll()
              << " encoding=" << a.Scenario(9030).encoding() << '\n';
  }
}
