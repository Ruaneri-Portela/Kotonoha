// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/LucaSystemScript.hpp>

#include <Kotonoha/localization/Encoding.hpp>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <locale>
#include <stdexcept>

namespace LucaSystem {

namespace {

uint16_t readU16(const std::vector<std::uint8_t> &data, size_t off) {
  if (off + 2 > data.size())
    throw std::runtime_error("Script: truncated uint16 at offset " +
                             std::to_string(off));
  return static_cast<uint16_t>(data[off]) |
         (static_cast<uint16_t>(data[off + 1]) << 8);
}

// Collect printable, non-control Shift-JIS segments split on NUL bytes.
bool isMeaningful(const std::string &text) {
  if (text.size() < 2)
    return false;
  int letters = 0;
  for (char c : text) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || static_cast<unsigned char>(c) >= 0x80)
      ++letters;
  }
  return letters * 2 >= int(text.size());
}

std::string readUtf16String(const std::vector<std::uint8_t> &bytes,
                            size_t &cursor) {
  if (cursor + 2 > bytes.size())
    return {};
  const auto units = size_t(bytes[cursor]) | (size_t(bytes[cursor + 1]) << 8);
  cursor += 2;
  if (cursor + units * 2 > bytes.size())
    return {};
  std::u16string text;
  text.reserve(units);
  for (size_t i = 0; i < units; ++i)
    text.push_back(char16_t(bytes[cursor + i * 2]) |
                   (char16_t(bytes[cursor + i * 2 + 1]) << 8));
  cursor += units * 2;
  if (cursor + 2 <= bytes.size() && bytes[cursor] == 0 &&
      bytes[cursor + 1] == 0)
    cursor += 2;
  return Kotonoha::Utf16ToUtf8(text);
}

std::string readUtf8String(const std::vector<std::uint8_t> &bytes,
                           size_t &cursor) {
  if (cursor + 2 > bytes.size())
    return {};
  const auto encoded =
      unsigned(bytes[cursor]) | (unsigned(bytes[cursor + 1]) << 8);
  cursor += 2;
  const size_t length = encoded ? 0x10000u - encoded : 0;
  if (cursor + length > bytes.size())
    return {};
  std::string text(bytes.begin() + cursor, bytes.begin() + cursor + length);
  cursor += length;
  if (cursor < bytes.size() && bytes[cursor] == 0)
    ++cursor;
  return text;
}

} // namespace

std::string ReadLsUtf16(const std::vector<std::uint8_t> &bytes,
                        size_t &cursor) {
  return readUtf16String(bytes, cursor);
}

std::string ReadLsUtf8(const std::vector<std::uint8_t> &bytes, size_t &cursor) {
  if (cursor + 2 > bytes.size())
    return {};
  const unsigned encoded =
      unsigned(bytes[cursor]) | (unsigned(bytes[cursor + 1]) << 8);
  cursor += 2;
  const size_t length =
      encoded == 0
          ? 0u
          : (encoded >= 0x8000u ? (0x10000u - encoded) : size_t(encoded));
  if (cursor + length > bytes.size())
    return {};
  std::string text(bytes.begin() + cursor, bytes.begin() + cursor + length);
  cursor += length;
  if (cursor < bytes.size() && bytes[cursor] == 0)
    ++cursor;
  return text;
}

std::string ReadLsCString(const std::vector<std::uint8_t> &bytes,
                          size_t &cursor) {
  if (cursor >= bytes.size())
    return {};
  size_t end = cursor;
  while (end < bytes.size() && bytes[end] != 0)
    ++end;
  std::string text(bytes.begin() + cursor, bytes.begin() + end);
  cursor = end < bytes.size() ? end + 1 : end;
  return text;
}

std::optional<VarStrSetEntry>
ParseVarStrSet(const ScriptInstruction &instruction) {
  const auto &bytes = instruction.paramBytes;
  if (bytes.size() < 6)
    return std::nullopt;
  size_t cursor = 2; // skip the leading uint16 constant
  VarStrSetEntry entry;
  entry.scenario = ReadLsCString(bytes, cursor);
  if (cursor + 2 > bytes.size())
    return std::nullopt;
  entry.id = static_cast<std::uint16_t>(
      bytes[cursor] | (static_cast<std::uint16_t>(bytes[cursor + 1]) << 8));
  cursor += 2;

  const std::string jp = ReadLsUtf16(bytes, cursor);
  const std::string en = ReadLsUtf16(bytes, cursor);
  const std::string zh = ReadLsUtf16(bytes, cursor);
  entry.text = !en.empty() ? en : (!jp.empty() ? jp : zh);
  if (entry.scenario.empty() || entry.text.empty())
    return std::nullopt;
  return entry;
}

Script::Script(std::string name, std::vector<std::uint8_t> data)
    : name_(std::move(name)), data_(std::move(data)) {
  parse();
}

void Script::parse() {
  if (data_.size() < 4) {
    valid_ = false;
    error_ = "script too small: " + name_;
    return;
  }

  size_t pos = 0;
  const size_t n = data_.size();
  while (pos + 4 <= n) {
    ScriptInstruction ins;
    ins.pos = pos;
    // LuckSystem's CodeLine layout is Len, Opcode, FixedFlag.
    ins.len = readU16(data_, pos);
    ins.opcode = data_[pos + 2];
    ins.fixedFlag = data_[pos + 3];

    const size_t padded = (size_t(ins.len) + 1u) & ~size_t(1);
    if (ins.len < 4 || padded > n - pos) {
      error_ = "script instruction at offset " + std::to_string(pos) +
               " has invalid or truncated len " + std::to_string(ins.len);
      valid_ = false;
      return;
    }

    // LuckSystem CodeLine: Len INCLUDES the entire payload, and Align
    // follows the instruction only when Len is odd. Do not discard the
    // final payload byte on odd-length instructions.
    ins.rawBytes.assign(data_.begin() + pos + 4, data_.begin() + pos + ins.len);
    ins.align.assign(data_.begin() + pos + ins.len,
                     data_.begin() + pos + padded);

    // Fixed parameters (little-endian uint16 values), per fixedFlag.
    const size_t fixedSize = ins.fixedFlag >= 2   ? 4u
                             : ins.fixedFlag == 1 ? 2u
                                                  : 0u;
    if (ins.rawBytes.size() < fixedSize) {
      error_ = "script instruction at offset " + std::to_string(pos) +
               " has truncated fixed parameters";
      valid_ = false;
      return;
    }
    size_t cursor = 0;
    if (ins.fixedFlag >= 2 && cursor + 4 <= ins.rawBytes.size()) {
      ins.fixedParam.push_back(ins.rawBytes[cursor]);
      ins.fixedParam.push_back(ins.rawBytes[cursor + 1]);
      cursor += 2;
      ins.fixedParam.push_back(ins.rawBytes[cursor]);
      ins.fixedParam.push_back(ins.rawBytes[cursor + 1]);
      cursor += 2;
    } else if (ins.fixedFlag == 1 && cursor + 2 <= ins.rawBytes.size()) {
      ins.fixedParam.push_back(ins.rawBytes[cursor]);
      ins.fixedParam.push_back(ins.rawBytes[cursor + 1]);
      cursor += 2;
    }
    ins.paramBytes.assign(ins.rawBytes.begin() + cursor, ins.rawBytes.end());

    // AIR opcode 36 is MESSAGE. Its payload is a fixed uint16 followed by
    // Japanese UTF-16, English UTF-8 and Chinese UTF-16 length strings.
    if (ins.opcode == 36 && ins.paramBytes.size() >= 2) {
      size_t text = 2;
      auto japanese = readUtf16String(ins.paramBytes, text);
      // Empty Japanese strings are valid. The next field is still
      // the length-prefixed UTF-8 translation (LuckSystem AIR layout).
      auto english = readUtf8String(ins.paramBytes, text);
      if (!english.empty())
        ins.text = std::move(english);
      else if (!japanese.empty())
        ins.text = std::move(japanese);
      if (!ins.text.empty())
        strings_.push_back(ins.text);
    }

    instructions_.push_back(std::move(ins));

    // Advance to next instruction: ((len + 1) & ~1)
    pos += (size_t(instructions_.back().len) + 1u) & ~size_t(1);
  }

  if (pos != n) {
    error_ = "script has truncated trailing instruction at offset " +
             std::to_string(pos);
    valid_ = false;
    return;
  }
  valid_ = true;
}

std::vector<std::uint8_t> Script::serialize() const {
  if (!valid_)
    throw std::runtime_error("Cannot serialize malformed LucaSystem script: " +
                             error_);
  std::vector<std::uint8_t> output;
  output.reserve(data_.size());
  for (const auto &ins : instructions_) {
    if (ins.pos != output.size() || ins.len < 4 ||
        ins.rawBytes.size() != size_t(ins.len) - 4 ||
        ins.align.size() != (ins.len & 1u))
      throw std::runtime_error("Inconsistent LucaSystem instruction layout");
    output.push_back(static_cast<std::uint8_t>(ins.len));
    output.push_back(static_cast<std::uint8_t>(ins.len >> 8));
    output.push_back(ins.opcode);
    output.push_back(ins.fixedFlag);
    output.insert(output.end(), ins.rawBytes.begin(), ins.rawBytes.end());
    output.insert(output.end(), ins.align.begin(), ins.align.end());
  }
  if (output.size() != data_.size())
    throw std::runtime_error("LucaSystem script byte size changed");
  return output;
}

std::vector<std::uint8_t>
Script::replaceRaw(std::size_t instructionIndex,
                   const std::vector<std::uint8_t> &bytes) const {
  if (instructionIndex >= instructions_.size())
    throw std::out_of_range("LucaSystem instruction index");
  const auto &ins = instructions_[instructionIndex];
  if (bytes.size() != ins.rawBytes.size())
    throw std::invalid_argument(
        "LucaSystem payload replacements must preserve byte length");
  auto result = serialize();
  std::copy(bytes.begin(), bytes.end(),
            result.begin() + static_cast<std::size_t>(ins.pos) + 4);
  return result;
}

std::vector<std::uint8_t> Script::replaceRawAtOffsets(
    const std::map<std::uint32_t, std::vector<std::uint8_t>> &edits) const {
  auto output = serialize();
  for (const auto &[offset, payload] : edits) {
    const auto index = FindInstructionAtOffset(instructions_, offset);
    if (!index)
      throw std::out_of_range(name_ + ": no instruction at offset " +
                              std::to_string(offset));
    const auto &instruction = instructions_[*index];
    if (payload.size() != instruction.rawBytes.size())
      throw std::invalid_argument(
          name_ + ": payload size differs at offset " +
          std::to_string(offset) + " (expected " +
          std::to_string(instruction.rawBytes.size()) + " bytes)");
    std::copy(payload.begin(), payload.end(),
              output.begin() + static_cast<std::size_t>(offset) + 4);
  }
  return output;
}

namespace {

using Bytes = std::vector<uint8_t>;

// The offsets here refer to the raw payload, including the fixed parameters.
// Decode only explicit AIR fields, never search arbitrary payloads for numbers
// that happen to match instruction offsets.
size_t stringEnd(const Bytes &bytes, size_t start, bool utf16) {
  const auto count = readU16(bytes, start);
  const size_t length =
      utf16 ? size_t(count) * 2 : (count ? 0x10000u - count : 0u);
  const size_t terminator = utf16 ? 2 : 1;
  const auto end = start + 2 + length;
  if (end + terminator > bytes.size() || bytes[end] != 0 ||
      (utf16 && bytes[end + 1] != 0))
    throw std::runtime_error("truncated or unterminated AIR string field");
  return end + terminator;
}

Bytes patchMessage(const ScriptInstruction &ins, const std::string &text) {
  if (ins.opcode != 36)
    throw std::runtime_error("edit does not refer to MESSAGE");
  if (text.size() > UINT16_MAX - 4 || text.find('\0') != std::string::npos)
    throw std::runtime_error("invalid AIR MESSAGE text length or NUL byte");
  const char *next = text.data();
  size_t left = text.size();
  while (left > 0) {
    const auto before = left;
    if (SDL_StepUTF8(&next, &left) == SDL_INVALID_UNICODE_CODEPOINT &&
        before - left != 3)
      throw std::runtime_error("replacement is not valid UTF-8");
  }
  const auto &raw = ins.rawBytes;
  const auto begin = stringEnd(raw, ins.fixedParam.size() + 2, true);
  const auto end = stringEnd(raw, begin, false);
  // Verify the next field also starts on the expected boundary.
  stringEnd(raw, end, true);
  Bytes result(raw.begin(), raw.begin() + begin);
  const auto encoded = static_cast<uint16_t>(0u - text.size());
  result.push_back(static_cast<uint8_t>(encoded));
  result.push_back(static_cast<uint8_t>(encoded >> 8));
  result.insert(result.end(), text.begin(), text.end());
  result.push_back(0);
  result.insert(result.end(), raw.begin() + end, raw.end());
  if (result.size() > UINT16_MAX - 4)
    throw std::runtime_error("translated instruction exceeds 16-bit size");
  return result;
}

template <class Visit>
void visitAirTargets(const ScriptInstruction &ins, std::string_view current,
                     Visit visit) {
  const auto &raw = ins.rawBytes;
  size_t start = ins.fixedParam.size();
  size_t count = 1;
  auto destination = current;
  switch (ins.opcode) {
  case 15: // GOTO
  case 17: // GOSUB
    break;
  case 16: // ONGOTO
    start = stringEnd(raw, start, false);
    count = readU16(raw, start);
    start += 2;
    break;
  case 18: // IFY
  case 19: // IFN
    start = stringEnd(raw, start, false);
    break;
  case 21:   // JUMP
  case 22: { // FARCALL
    const auto end = stringEnd(raw, start, false);
    destination =
        std::string_view(reinterpret_cast<const char *>(raw.data() + start + 2),
                         end - start - 3);
    if (destination.empty() || destination.find('\0') != std::string_view::npos)
      throw std::runtime_error("invalid AIR jump scenario");
    start = end;
    if (ins.opcode == 21 && start == raw.size())
      return; // file-entry jump with no explicit byte address
    break;
  }
  default:
    return;
  }
  if (raw.size() - start != count * 4)
    throw std::runtime_error("unexpected AIR branch layout");
  for (size_t i = 0; i < count; ++i) {
    const auto field = start + i * 4;
    uint32_t target = 0;
    for (size_t byte = 0; byte < 4; ++byte)
      target |= uint32_t(raw[field + byte]) << (8 * byte);
    visit(field, destination, target);
  }
}

struct Layout {
  const Script *script = nullptr;
  std::vector<uint32_t> offsets;
  std::map<size_t, Bytes> payloads;
  uint32_t size = 0;
};

} // namespace

std::map<std::string, std::vector<uint8_t>>
PatchAirMessages(const std::map<std::string, Script> &scripts,
                 const AirMessageEdits &edits) {
  std::map<std::string_view, Layout> layouts;
  std::map<std::string, Bytes> result;
  for (const auto &[name, changes] : edits) {
    if (!scripts.count(name))
      throw std::runtime_error("Unknown AIR scenario: " + name);
  }
  for (const auto &[name, script] : scripts) {
    if (!script.valid())
      throw std::runtime_error(script.error());
    auto &layout = layouts[name];
    layout.script = &script;
    const auto &instructions = script.instructions();
    if (const auto scenarioEdits = edits.find(name);
        scenarioEdits != edits.end()) {
      for (const auto &[offset, text] : scenarioEdits->second) {
        const auto index = FindInstructionAtOffset(instructions, offset);
        if (!index)
          throw std::runtime_error(name + ": no instruction at byte " +
                                   std::to_string(offset));
        try {
          auto bytes = patchMessage(instructions[*index], text);
          if (bytes != instructions[*index].rawBytes)
            layout.payloads.emplace(*index, std::move(bytes));
        } catch (const std::exception &e) {
          throw std::runtime_error(name + " at byte " + std::to_string(offset) +
                                   ": " + e.what());
        }
      }
    }
    layout.offsets.reserve(instructions.size());
    uint64_t size = 0;
    for (size_t i = 0; i < instructions.size(); ++i) {
      layout.offsets.push_back(static_cast<uint32_t>(size));
      const auto edit = layout.payloads.find(i);
      const auto len = edit == layout.payloads.end() ? instructions[i].len
                                                     : edit->second.size() + 4;
      size += (len + 1u) & ~size_t(1);
      if (size > UINT32_MAX)
        throw std::runtime_error(name + ": translated script exceeds 4 GiB");
    }
    layout.size = static_cast<uint32_t>(size);
  }
  for (const auto &[name, script] : scripts) {
    auto &layout = layouts.at(name);
    const auto &instructions = script.instructions();
    for (size_t i = 0; i < instructions.size(); ++i) {
      const auto &ins = instructions[i];
      try {
        visitAirTargets(
            ins, name,
            [&](size_t field, std::string_view dest, uint32_t original) {
              const auto target = layouts.find(dest);
              if (target == layouts.end())
                throw std::runtime_error("unknown jump scenario: " +
                                         std::string(dest));
              const auto index = FindInstructionAtOffset(
                  target->second.script->instructions(), original);
              if (!index)
                throw std::runtime_error(
                    "jump does not land on an instruction: " +
                    std::to_string(original));
              const auto updated = target->second.offsets[*index];
              if (updated == original)
                return;
              auto &raw =
                  layout.payloads.try_emplace(i, ins.rawBytes).first->second;
              for (size_t byte = 0; byte < 4; ++byte)
                raw[field + byte] = static_cast<uint8_t>(updated >> (8 * byte));
            });
      } catch (const std::exception &e) {
        throw std::runtime_error(name + " at byte " + std::to_string(ins.pos) +
                                 ": " + e.what());
      }
    }
    if (layout.payloads.empty())
      continue;
    auto &bytes = result[name];
    bytes.reserve(layout.size);
    for (size_t i = 0; i < instructions.size(); ++i) {
      const auto &ins = instructions[i];
      const auto change = layout.payloads.find(i);
      const auto &raw =
          change == layout.payloads.end() ? ins.rawBytes : change->second;
      const auto len = raw.size() + 4;
      bytes.push_back(static_cast<uint8_t>(len));
      bytes.push_back(static_cast<uint8_t>(len >> 8));
      bytes.push_back(ins.opcode);
      bytes.push_back(ins.fixedFlag);
      bytes.insert(bytes.end(), raw.begin(), raw.end());
      if (len & 1u)
        bytes.push_back(ins.align.empty() ? 0 : ins.align.front());
    }
  }
  return result;
}

std::optional<std::size_t>
FindInstructionAtOffset(const std::vector<ScriptInstruction> &instructions,
                        std::uint64_t offset) {
  const auto it =
      std::lower_bound(instructions.begin(), instructions.end(), offset,
                       [](const ScriptInstruction &ins, std::uint64_t target) {
                         return ins.pos < target;
                       });
  if (it == instructions.end() || it->pos != offset)
    return std::nullopt;
  return static_cast<std::size_t>(it - instructions.begin());
}

std::optional<std::size_t>
FindJumpPoint(const std::vector<ScriptInstruction> &instructions,
              std::string_view name) {
  if (name.empty())
    return std::nullopt;
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    const auto &ins = instructions[i];
    if (ins.opcode != 24 && ins.opcode != 38)
      continue; // LucaSystem JUMPPOINT (0x18 = 24)
    if (ins.text == name)
      return i + 1;
    const auto end = std::find(ins.paramBytes.begin(), ins.paramBytes.end(), 0);
    const std::string_view param(
        reinterpret_cast<const char *>(ins.paramBytes.data()),
        static_cast<std::size_t>(end - ins.paramBytes.begin()));
    if (param == name)
      return i + 1;
  }
  return std::nullopt;
}

std::string Disassemble(const ScriptInstruction &ins) {
  static const char *names[] = {"EQU",
                                "EQUN",
                                "EQUV",
                                "ADD",
                                "SUB",
                                "MUL",
                                "DIV",
                                "MOD",
                                "AND",
                                "OR",
                                "RANDOM",
                                "VARSTR",
                                "VARSTR_ADD",
                                "SET",
                                "FLAGCLR",
                                "GOTO",
                                "ONGOTO",
                                "GOSUB",
                                "IFY",
                                "IFN",
                                "RETURN",
                                "JUMP",
                                "FARCALL",
                                "FARRETURN",
                                "JUMPPOINT",
                                "END",
                                "STARTUP_BEGIN",
                                "STARTUP_END",
                                "TASKSCRVAR",
                                "VARSTR_SET",
                                "VARSTR_ALLOC",
                                "ARFLAGSET",
                                "COLORBG_SET",
                                "SPLINE_SET",
                                "SHAKELIST_SET",
                                "SCISSOR_TRIANGLELIST_SET",
                                "MESSAGE",
                                "MESSAGE_CLEAR",
                                "MESSAGE_WAIT",
                                "MESSAGE_AR_SET",
                                "SELECT",
                                "CLOSE_WINDOW",
                                "FADE_WINDOW",
                                "LOG_BEGIN",
                                "LOG_BREAK",
                                "LOG_END",
                                "VOICE",
                                "VOICE_STOP",
                                "WAIT_COUNT",
                                "WAIT_TIME",
                                "WAIT_TEXTFEED",
                                "COUNTER_WAIT",
                                "FFSTOP",
                                "INIT",
                                "STOP",
                                "IMAGELOAD",
                                "IMAGEUPDATE",
                                "ARC",
                                "MOVE",
                                "MOVE_SKIP",
                                "ROT",
                                "PEND",
                                "FADE",
                                "SCALE",
                                "SHAKE",
                                "SHAKELIST",
                                "BASE",
                                "MCMOVE",
                                "MCARC",
                                "MCROT",
                                "MCSHAKE",
                                "MCFADE",
                                "WAIT",
                                "WAIT_BSKIP",
                                "DRAW",
                                "WIPE",
                                "FRAMEON",
                                "FRAMEOFF",
                                "FW",
                                "SCISSOR",
                                "DELAY",
                                "RASTER",
                                "TONE",
                                "SCALECOSSIN",
                                "BMODE",
                                "SIZE",
                                "SPLINE",
                                "DISP",
                                "MASK",
                                "FACE",
                                "SEPIA",
                                "SEPIA_COLOR",
                                "CUSTOMMOVE",
                                "SWAP",
                                "ADDCOLOR",
                                "SUBCOLOR",
                                "SATURATION",
                                "CONTRAST",
                                "PRIORITY",
                                "UVWH",
                                "EVSCROLL",
                                "COLORLEVEL",
                                "NEGA",
                                "TONECURVE",
                                "SKIP_SCOPE_BEGIN",
                                "SKIP_SCOPE_END",
                                "QUAKE",
                                "BGM",
                                "BGM_WAIT_START",
                                "BGM_WAIT_FADE",
                                "BGM_PUSH",
                                "BGM_POP",
                                "BGM_ASYNC_FADE_STOP",
                                "SE",
                                "SE_STOP",
                                "SE_WAIT",
                                "SE_WAIT_COUNT",
                                "SE_WAIT_FADE",
                                "VOLUME",
                                "MOVIE",
                                "SETCGFLAG",
                                "EX",
                                "TROPHY",
                                "SETBGMFLAG",
                                "TASK",
                                "PRINTF",
                                "DIALOG",
                                "VIB_PLAY",
                                "VIB_FILE",
                                "VIB_STOP",
                                "CHAR_VOLUME",
                                "SCENE_REPLAY_END",
                                "SAVE_THUMBNAIL",
                                "TEXTURE_PREFETCH",
                                "CLOSE_PRELOAD_FILE",
                                "RESERVED_135",
                                "MANPU",
                                "SCENARIO",
                                "HAIKEI_SET",
                                "UNKNOWN"};

  std::string out;
  if (ins.opcode < sizeof(names) / sizeof(names[0]))
    out += names[ins.opcode];
  else
    out += "OP_" + std::to_string(ins.opcode);
  out += " ff=";
  out += std::to_string(ins.fixedFlag);
  out += " len=";
  out += std::to_string(ins.len);
  out += " raw=";
  for (std::uint8_t b : ins.rawBytes) {
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02x", b);
    out += buf;
  }
  if (!ins.fixedParam.empty()) {
    out += " fixed=";
    for (size_t i = 0; i + 1 < ins.fixedParam.size(); i += 2) {
      uint16_t v = static_cast<uint16_t>(ins.fixedParam[i]) |
                   (static_cast<uint16_t>(ins.fixedParam[i + 1]) << 8);
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%u", v);
      out += buf;
      out += " ";
    }
  }
  return out;
}

std::vector<std::string> ExtractShiftJisStrings(std::string_view bytes) {
  std::vector<std::string> result;
  size_t i = 0;
  while (i < bytes.size()) {
    if (static_cast<unsigned char>(bytes[i]) == 0) {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < bytes.size() && static_cast<unsigned char>(bytes[j]) != 0)
      ++j;
    std::string segment(bytes.data() + i, j - i);
    try {
      std::string utf8 = Kotonoha::ShiftJisToUtf8(segment);
      if (!utf8.empty())
        result.push_back(std::move(utf8));
    } catch (...) {
      // Undecodable segment: skip.
    }
    i = j;
  }
  return result;
}

} // namespace LucaSystem
