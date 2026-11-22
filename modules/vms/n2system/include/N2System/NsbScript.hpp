// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace N2System {

struct NsbInstruction {
  std::uint32_t line = 0;
  std::uint16_t opcode = 0;
  std::uint32_t byteOffset = 0;
  std::vector<std::string> args;
};

struct SystemIniConfig {
  std::string productName;
  std::string version;
  std::string startScript = "nss/0_boot.nss";
  std::string closeScript = "nss/sys_close.nss";
  std::string menuScript = "nss/sys_menu.nss";
  std::string backlogScript = "nss/sys_backlog.nss";
  std::string windowTitle = "STEINS;GATE";
  int bgmVolume = 600;
  int seVolume = 800;
  int voiceVolume = 800;
  int width = 1024;
  int height = 576;

  static SystemIniConfig parseUtf16Le(const std::vector<std::uint8_t> &bytes);
  static SystemIniConfig parseUtf8(std::string_view text);
};

// Parsed Steins;Gate compiled .nsb bytecode + .map symbol table.
class NsbScript {
public:
  NsbScript() = default;

  static NsbScript parse(std::string scriptPath,
                         const std::vector<std::uint8_t> &nsbBytes,
                         const std::vector<std::uint8_t> &mapBytes);

  const std::string &path() const { return path_; }
  const std::vector<NsbInstruction> &instructions() const {
    return instructions_;
  }
  const std::vector<std::string> &includes() const { return includes_; }
  const std::unordered_map<std::string, std::size_t> &symbols() const {
    return symbols_;
  }

  // Resolve any symbol (label.*, chapter.*, scene.*, function.*) to an
  // instruction index, or npos if not found.
  std::size_t findSymbol(std::string_view name) const;
  std::size_t findChapter(std::string_view chapterName) const;
  std::size_t findScene(std::string_view sceneName) const;
  std::size_t findFunction(std::string_view functionName) const;

  static std::string decodeUtf16Le(const std::uint8_t *data,
                                   std::size_t byteLen);
  static std::string nssToNsbPath(std::string_view nssPath);
  static std::string nssToMapPath(std::string_view nssPath);

private:
  std::string path_;
  std::vector<NsbInstruction> instructions_;
  std::unordered_map<std::string, std::size_t> symbols_;
  std::vector<std::string> includes_;
};

} // namespace N2System
