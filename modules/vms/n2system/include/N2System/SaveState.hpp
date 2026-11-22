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
  // Versioned runtime extension, omitted in legacy N2SV saves. This captures
  // the live NSB VM, scene graph and phone state without changing the v1
  // variables/backlog layout. Not a pointer/native-struct memory dump.
  std::vector<uint8_t> runtimeBlob;
};

class SaveStateCodec {
public:
  static std::string serializeJson(const SaveData &data);
  static bool deserializeJson(std::string_view jsonStr, SaveData &outData);

  static std::vector<uint8_t> serializeBinary(const SaveData &data);
  static bool deserializeBinary(const std::vector<uint8_t> &bytes, SaveData &outData);
};

} // namespace N2System
