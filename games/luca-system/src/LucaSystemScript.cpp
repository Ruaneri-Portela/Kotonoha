// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <LucaSystem/LucaSystemScript.hpp>

#include <Kotonoha/localization/Encoding.hpp>

#include <algorithm>
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

    if (ins.len < 4 || pos + ins.len > n) {
      error_ = "script instruction at offset " + std::to_string(pos) +
               " has invalid len " + std::to_string(ins.len);
      valid_ = false;
      return;
    }

    ins.rawBytes.assign(data_.begin() + pos + 4,
                        data_.begin() + pos + ins.len - (ins.len & 1));
    ins.align.assign(data_.begin() + pos + ins.len - (ins.len & 1),
                     data_.begin() + pos + ins.len);

    // Fixed parameters (little-endian uint16 values), per fixedFlag.
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
      auto english = japanese.empty() ? std::string()
                                      : readUtf8String(ins.paramBytes, text);
      if (!english.empty())
        ins.text = std::move(english);
      else if (!japanese.empty())
        ins.text = std::move(japanese);
      if (!ins.text.empty())
        strings_.push_back(ins.text);
    }

    instructions_.push_back(std::move(ins));

    // Advance to next instruction: ((len + 1) & ~1)
    pos += ((instructions_.back().len + 1) & ~1u);
  }

  valid_ = true;
}

std::string Disassemble(const ScriptInstruction &ins) {
  std::string out;
  out += std::to_string(ins.opcode);
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
