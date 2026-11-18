// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <N2System/DialogueSystem.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace N2System {

struct SaveData {
  std::string chapterName;
  std::size_t position = 0;

  std::map<std::string, int64_t> integers;
  std::map<std::string, std::string> strings;
  std::map<std::string, bool> flags;

  std::string activeSpeaker;
  std::string activeText;
  std::string activeVoice;

  std::vector<DialogueBacklogEntry> backlog;
};

class SaveStateCodec {
public:
  static std::string serializeJson(const SaveData &data);
  static bool deserializeJson(std::string_view jsonStr, SaveData &outData);

  static std::vector<uint8_t> serializeBinary(const SaveData &data);
  static bool deserializeBinary(const std::vector<uint8_t> &bytes, SaveData &outData);
};

} // namespace N2System
