#pragma once

// Shared, SDL-free view of LucaSystem bytecode for the Debug UI.
// This is a read-only inspector: building the catalog never executes the VM.
#include <LucaSystem/LucaSystemScript.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace LucaSystem {

struct DebugInstruction {
  std::size_t pc = 0;
  std::string label;
  std::vector<std::string> assets;
};

// The script asset itself and logical IDs of referenced image, voice, music
// and sound resources. Opcodes mirror the AIR VM switch in LucaSystemRuntime.
std::vector<DebugInstruction>
buildDebugInstructions(const std::string &script,
                       const std::vector<ScriptInstruction> &instructions,
                       std::size_t currentPc);

} // namespace LucaSystem
