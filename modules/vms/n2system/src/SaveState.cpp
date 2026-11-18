// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/SaveState.hpp>
#include <cstring>
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
  uint32_t len = in[cursor] | (in[cursor + 1] << 8) | (in[cursor + 2] << 16) |
                 (in[cursor + 3] << 24);
  cursor += 4;
  if (cursor + len > in.size())
    return false;
  outStr.assign(reinterpret_cast<const char *>(&in[cursor]), len);
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

std::string SaveStateCodec::serializeJson(const SaveData &data) {
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"chapter\": \"" << data.chapterName << "\",\n";
  ss << "  \"position\": " << data.position << ",\n";
  ss << "  \"activeSpeaker\": \"" << data.activeSpeaker << "\",\n";
  ss << "  \"activeText\": \"" << data.activeText << "\",\n";
  ss << "  \"activeVoice\": \"" << data.activeVoice << "\",\n";

  ss << "  \"integers\": {\n";
  size_t i = 0;
  for (const auto &[k, v] : data.integers) {
    ss << "    \"" << k << "\": " << v;
    if (++i < data.integers.size())
      ss << ",";
    ss << "\n";
  }
  ss << "  },\n";

  ss << "  \"strings\": {\n";
  i = 0;
  for (const auto &[k, v] : data.strings) {
    ss << "    \"" << k << "\": \"" << v << "\"";
    if (++i < data.strings.size())
      ss << ",";
    ss << "\n";
  }
  ss << "  },\n";

  ss << "  \"flags\": {\n";
  i = 0;
  for (const auto &[k, v] : data.flags) {
    ss << "    \"" << k << "\": " << (v ? "true" : "false");
    if (++i < data.flags.size())
      ss << ",";
    ss << "\n";
  }
  ss << "  }\n";

  ss << "}\n";
  return ss.str();
}

bool SaveStateCodec::deserializeJson(std::string_view jsonStr,
                                     SaveData &outData) {
  if (jsonStr.empty())
    return false;

  auto findString = [&](std::string_view key) -> std::string {
    auto pos = jsonStr.find(key);
    if (pos == std::string_view::npos)
      return "";
    pos = jsonStr.find(':', pos);
    if (pos == std::string_view::npos)
      return "";
    pos = jsonStr.find('"', pos);
    if (pos == std::string_view::npos)
      return "";
    auto end = jsonStr.find('"', pos + 1);
    if (end == std::string_view::npos)
      return "";
    return std::string(jsonStr.substr(pos + 1, end - pos - 1));
  };

  outData.chapterName = findString("\"chapter\"");
  outData.activeSpeaker = findString("\"activeSpeaker\"");
  outData.activeText = findString("\"activeText\"");
  outData.activeVoice = findString("\"activeVoice\"");

  auto pos = jsonStr.find("\"position\"");
  if (pos != std::string_view::npos) {
    pos = jsonStr.find(':', pos);
    if (pos != std::string_view::npos) {
      outData.position = static_cast<std::size_t>(
          std::stoull(std::string(jsonStr.substr(pos + 1))));
    }
  }

  return !outData.chapterName.empty();
}

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

  return out;
}

bool SaveStateCodec::deserializeBinary(const std::vector<uint8_t> &bytes,
                                       SaveData &outData) {
  if (bytes.size() < 4 || bytes[0] != 'N' || bytes[1] != '2' ||
      bytes[2] != 'S' || bytes[3] != 'V')
    return false;

  size_t cursor = 4;
  uint64_t temp64 = 0;

  if (!readString(bytes, cursor, outData.chapterName))
    return false;
  if (!readU64(bytes, cursor, temp64))
    return false;
  outData.position = static_cast<size_t>(temp64);

  if (!readString(bytes, cursor, outData.activeSpeaker))
    return false;
  if (!readString(bytes, cursor, outData.activeText))
    return false;
  if (!readString(bytes, cursor, outData.activeVoice))
    return false;

  // Integers
  if (!readU64(bytes, cursor, temp64))
    return false;
  size_t intCount = static_cast<size_t>(temp64);
  outData.integers.clear();
  for (size_t i = 0; i < intCount; ++i) {
    std::string key;
    uint64_t val = 0;
    if (!readString(bytes, cursor, key) || !readU64(bytes, cursor, val))
      return false;
    outData.integers[key] = static_cast<int64_t>(val);
  }

  // Strings
  if (!readU64(bytes, cursor, temp64))
    return false;
  size_t strCount = static_cast<size_t>(temp64);
  outData.strings.clear();
  for (size_t i = 0; i < strCount; ++i) {
    std::string key, val;
    if (!readString(bytes, cursor, key) || !readString(bytes, cursor, val))
      return false;
    outData.strings[key] = val;
  }

  // Flags
  if (!readU64(bytes, cursor, temp64))
    return false;
  size_t flagCount = static_cast<size_t>(temp64);
  outData.flags.clear();
  for (size_t i = 0; i < flagCount; ++i) {
    std::string key;
    if (!readString(bytes, cursor, key) || cursor >= bytes.size())
      return false;
    outData.flags[key] = (bytes[cursor++] != 0);
  }

  // Backlog
  if (cursor < bytes.size()) {
    if (!readU64(bytes, cursor, temp64))
      return false;
    size_t bgCount = static_cast<size_t>(temp64);
    outData.backlog.clear();
    for (size_t i = 0; i < bgCount; ++i) {
      DialogueBacklogEntry entry;
      if (!readString(bytes, cursor, entry.speaker) ||
          !readString(bytes, cursor, entry.text) ||
          !readString(bytes, cursor, entry.voicePath))
        return false;
      outData.backlog.push_back(std::move(entry));
    }
  }

  return true;
}

} // namespace N2System
