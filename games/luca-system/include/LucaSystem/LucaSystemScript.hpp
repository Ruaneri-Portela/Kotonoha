#pragma once

// LucaSystem script bytecode reader / disassembler.
//
// The script format is decoded following wetor/LuckSystem (script.go). A
// script is a sequence of instructions, each:
//   uint16 len         unpadded instruction length incl. the 4-byte header
//   uint8  opcode
//   uint8  fixedFlag   (0: no fixed params, 1: one uint16, 2+: two uint16)
//   raw bytes (len - 4)
//   align byte (only when len is odd; not included in len)
// The instruction stream advances by ((len + 1) & ~1) and is read until EOF.
//
// Opcodes and their parameter semantics are game-specific (defined by an
// external OPCODE.txt per title, as in LuckSystem). This reader decodes the
// structure and extracts the Shift-JIS text strings embedded in the
// parameters, without assuming a particular opcode table.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
  // Lossless byte-for-byte reconstruction of parsed CodeLine instructions.
  // Useful for preservation tests and future opcode-aware script patching.
  // Throws if parsing failed or the decoded instruction layout is inconsistent.
  std::vector<std::uint8_t> serialize() const;
  // Same-size instruction payload replacement for translation/patch tools.
  // Offsets and unknown opcodes are preserved; changing instruction size
  // requires a separate branch-relocation implementation.
  std::vector<std::uint8_t> replaceRaw(std::size_t instructionIndex,
                                       const std::vector<std::uint8_t> &bytes) const;

private:
  std::string name_;
  std::vector<std::uint8_t> data_;
  std::vector<ScriptInstruction> instructions_;
  std::vector<std::string> strings_;
  bool valid_ = false;
  std::string error_;
  void parse();
};

// Jump destinations from LuckSystem's 32-bit byte offsets. These helpers
// accept only exact instruction boundaries to avoid corrupt control flow.
std::optional<std::size_t> FindInstructionAtOffset(
    const std::vector<ScriptInstruction>& instructions, std::uint64_t offset);
// The older title-specific JUMPPOINT form, never arbitrary opcode payloads.
std::optional<std::size_t> FindJumpPoint(
    const std::vector<ScriptInstruction>& instructions, std::string_view name);

// Disassemble one instruction into a human readable line.
std::string Disassemble(const ScriptInstruction &inst);

// Extract Shift-JIS text runs from a byte buffer (null-terminated segments,
// converted with Kotonoha::ShiftJisToUtf8).
std::vector<std::string> ExtractShiftJisStrings(std::string_view bytes);

// Decode LucaSystem length-prefixed UTF-8 (len = encoded ? 0x10000 - encoded : 0)
// and UTF-16LE strings from instruction parameter payloads.
std::string ReadLsUtf8(const std::vector<std::uint8_t> &bytes, size_t &cursor);
std::string ReadLsUtf16(const std::vector<std::uint8_t> &bytes, size_t &cursor);

} // namespace LucaSystem
