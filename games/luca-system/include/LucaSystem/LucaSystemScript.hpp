#pragma once

// LucaSystem script bytecode reader / disassembler.
//
// The script format is decoded following wetor/LuckSystem (script.go). A
// script is a sequence of instructions, each:
//   uint8  opcode
//   uint8  fixedFlag   (0: no fixed params, 1: one uint16, 2+: two uint16)
//   uint16 len         total instruction length incl. this 4-byte header
//   raw bytes (len - 4)
//   align  (len & 1)
// The instruction stream advances by ((len + 1) & ~1) and is read until EOF.
//
// Opcodes and their parameter semantics are game-specific (defined by an
// external OPCODE.txt per title, as in LuckSystem). This reader decodes the
// structure and extracts the Shift-JIS text strings embedded in the
// parameters, without assuming a particular opcode table.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace LucaSystem {

// One decoded LucaSystem script instruction.
struct ScriptInstruction {
  uint64_t pos = 0; // file offset of the instruction
  uint8_t opcode = 0;
  uint8_t fixedFlag = 0;
  uint16_t len = 0; // total instruction length (incl. header)
  std::vector<std::uint8_t> fixedParam; // 0-2 little-endian uint16 values
  std::vector<std::uint8_t> paramBytes; // remaining parameter bytes
  std::vector<std::uint8_t> rawBytes;   // all parameter bytes
  std::vector<std::uint8_t> align;
  std::string text;
};

// Parsed LucaSystem script (the decoded contents of one SCRIPT.PAK entry).
class Script {
public:
  Script(std::string name, std::vector<std::uint8_t> data);

  const std::string &name() const { return name_; }
  const std::vector<ScriptInstruction> &instructions() const {
    return instructions_;
  }
  size_t instructionCount() const { return instructions_.size(); }
  // Shift-JIS text strings found inside parameter bytes, in order of
  // appearance.
  const std::vector<std::string> &strings() const { return strings_; }

  bool valid() const { return valid_; }
  const std::string &error() const { return error_; }

private:
  std::string name_;
  std::vector<std::uint8_t> data_;
  std::vector<ScriptInstruction> instructions_;
  std::vector<std::string> strings_;
  bool valid_ = false;
  std::string error_;
  void parse();
};

// Disassemble one instruction into a human readable line.
std::string Disassemble(const ScriptInstruction &inst);

// Extract Shift-JIS text runs from a byte buffer (null-terminated segments,
// converted with Kotonoha::ShiftJisToUtf8).
std::vector<std::string> ExtractShiftJisStrings(std::string_view bytes);

} // namespace LucaSystem
