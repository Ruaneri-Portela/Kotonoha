// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/LucaSystemScript.hpp>

#include <Kotonoha/localization/Encoding.hpp>

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
