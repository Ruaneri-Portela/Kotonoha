// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/SaveState.hpp>
#include <cstring>
#include <limits>
#include <utility>
#include <sstream>

namespace N2System {

namespace {
void writeString(std::vector<uint8_t> &out, const std::string &str) {
  uint32_t len = static_cast<uint32_t>(str.size());
  out.push_back(len & 0xFF);
  out.push_back((len >> 8) & 0xFF);
  out.push_back((len >> 16) & 0xFF);
  out.push_back((len >> 24) & 0xFF);
  out.insert(out.end(), str.begin(), str.end());
}

bool readString(const std::vector<uint8_t> &in, size_t &cursor,
                std::string &outStr) {
  if (cursor + 4 > in.size())
    return false;
  const uint32_t len = uint32_t(in[cursor]) |
                       (uint32_t(in[cursor + 1]) << 8) |
                       (uint32_t(in[cursor + 2]) << 16) |
                       (uint32_t(in[cursor + 3]) << 24);
  cursor += 4;
  if (len > in.size() - cursor)
    return false;
  outStr.assign(reinterpret_cast<const char *>(in.data() + cursor), len);
  cursor += len;
  return true;
}

void writeU64(std::vector<uint8_t> &out, uint64_t val) {
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
}

bool readU64(const std::vector<uint8_t> &in, size_t &cursor, uint64_t &val) {
  if (cursor + 8 > in.size())
    return false;
  val = 0;
  for (int i = 0; i < 8; ++i)
    val |= (static_cast<uint64_t>(in[cursor + i]) << (i * 8));
  cursor += 8;
  return true;
}
} // namespace

std::vector<uint8_t> SaveStateCodec::serializeBinary(const SaveData &data) {
  std::vector<uint8_t> out;
  out.push_back('N');
  out.push_back('2');
  out.push_back('S');
  out.push_back('V');

  writeString(out, data.chapterName);
  writeU64(out, static_cast<uint64_t>(data.position));

  writeString(out, data.activeSpeaker);
  writeString(out, data.activeText);
  writeString(out, data.activeVoice);

  // Integers
  writeU64(out, static_cast<uint64_t>(data.integers.size()));
  for (const auto &[k, v] : data.integers) {
    writeString(out, k);
    writeU64(out, static_cast<uint64_t>(v));
  }

  // Strings
  writeU64(out, static_cast<uint64_t>(data.strings.size()));
  for (const auto &[k, v] : data.strings) {
    writeString(out, k);
    writeString(out, v);
  }

  // Flags
  writeU64(out, static_cast<uint64_t>(data.flags.size()));
  for (const auto &[k, v] : data.flags) {
    writeString(out, k);
    out.push_back(v ? 1 : 0);
  }

  // Backlog
  writeU64(out, static_cast<uint64_t>(data.backlog.size()));
  for (const auto &entry : data.backlog) {
    writeString(out, entry.speaker);
    writeString(out, entry.text);
    writeString(out, entry.voicePath);
  }

  if (!data.runtimeBlob.empty()) {
    // Explicit trailer: old N2SV readers can continue reading v1 fields.
    // New readers verify the entire extension before committing a save.
    out.insert(out.end(), {'N', '2', 'E', 'X'});
    writeU64(out, data.runtimeBlob.size());
    out.insert(out.end(), data.runtimeBlob.begin(), data.runtimeBlob.end());
  }
  return out;
}

bool SaveStateCodec::deserializeBinary(const std::vector<uint8_t> &bytes,
                                       SaveData &outData) {
  if (bytes.size() < 4 || bytes.size() > 32u * 1024u * 1024u ||
      bytes[0] != 'N' || bytes[1] != '2' ||
      bytes[2] != 'S' || bytes[3] != 'V')
    return false;

  SaveData parsed;
  size_t cursor = 4;
  uint64_t temp64 = 0;

  if (!readString(bytes, cursor, parsed.chapterName))
    return false;
  if (!readU64(bytes, cursor, temp64))
    return false;
  if (temp64 > std::numeric_limits<std::size_t>::max())
    return false;
  parsed.position = static_cast<std::size_t>(temp64);

  if (!readString(bytes, cursor, parsed.activeSpeaker))
    return false;
  if (!readString(bytes, cursor, parsed.activeText))
    return false;
  if (!readString(bytes, cursor, parsed.activeVoice))
    return false;

  // Integers
  if (!readU64(bytes, cursor, temp64))
    return false;
  if (temp64 > 100000 || temp64 > (bytes.size() - cursor) / 12)
    return false;
  size_t intCount = static_cast<size_t>(temp64);
  parsed.integers.clear();
  for (size_t i = 0; i < intCount; ++i) {
    std::string key;
    uint64_t val = 0;
    if (!readString(bytes, cursor, key) || !readU64(bytes, cursor, val))
      return false;
    parsed.integers[key] = static_cast<int64_t>(val);
  }

  // Strings
  if (!readU64(bytes, cursor, temp64))
    return false;
  if (temp64 > 100000 || temp64 > (bytes.size() - cursor) / 8)
    return false;
  size_t strCount = static_cast<size_t>(temp64);
  parsed.strings.clear();
  for (size_t i = 0; i < strCount; ++i) {
    std::string key, val;
    if (!readString(bytes, cursor, key) || !readString(bytes, cursor, val))
      return false;
    parsed.strings[key] = val;
  }

  // Flags
  if (!readU64(bytes, cursor, temp64))
    return false;
  if (temp64 > 100000 || temp64 > (bytes.size() - cursor) / 5)
    return false;
  size_t flagCount = static_cast<size_t>(temp64);
  parsed.flags.clear();
  for (size_t i = 0; i < flagCount; ++i) {
    std::string key;
    if (!readString(bytes, cursor, key) || cursor >= bytes.size())
      return false;
    parsed.flags[key] = (bytes[cursor++] != 0);
  }

  // Backlog
  if (cursor < bytes.size()) {
    if (!readU64(bytes, cursor, temp64))
      return false;
    if (temp64 > 100000 || temp64 > (bytes.size() - cursor) / 12)
      return false;
    size_t bgCount = static_cast<size_t>(temp64);
    parsed.backlog.clear();
    for (size_t i = 0; i < bgCount; ++i) {
      DialogueBacklogEntry entry;
      if (!readString(bytes, cursor, entry.speaker) ||
          !readString(bytes, cursor, entry.text) ||
          !readString(bytes, cursor, entry.voicePath))
        return false;
      parsed.backlog.push_back(std::move(entry));
    }
  }

  if (cursor < bytes.size()) {
    if (bytes.size() - cursor < 12 || bytes[cursor] != 'N' ||
        bytes[cursor + 1] != '2' || bytes[cursor + 2] != 'E' ||
        bytes[cursor + 3] != 'X')
      return false;
    cursor += 4;
    uint64_t length = 0;
    if (!readU64(bytes, cursor, length) || length > 16u * 1024u * 1024u ||
        length != bytes.size() - cursor)
      return false;
    parsed.runtimeBlob.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                              bytes.end());
    cursor = bytes.size();
  }
  if (cursor != bytes.size())
    return false;
  outData = std::move(parsed); // Atomic parse: leave caller unchanged on error.
  return true;
}

} // namespace N2System
