// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Artemis {

struct BgState {
  std::string file;
  std::string path;
  int x = 0;
  int y = 0;
  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  int zoom = 100;
  int alpha = 255;
  bool isSolidColor = false;
  std::uint8_t solidR = 0;
  std::uint8_t solidG = 0;
  std::uint8_t solidB = 0;
  int transitionMs = 0;
  std::string rule;
};

struct SpriteLayer {
  int id = 0;
  int lv = 5;
  std::string ch;
  std::string path;
  std::string file;
  std::string file1;
  std::string file2;
  int x = 0;
  int y = 0;
  int ax = 0;
  int ay = 0;
  int bx = 0;
  int by = 0;
  int alpha = 255;
  int zoom = 100;
  bool visible = true;
};

struct BgmState {
  std::string file;
  int volume = 100;
  bool playing = false;
  std::uint64_t generation = 0;
};

struct SeEvent {
  int id = 0;
  std::string file;
  int volume = 100;
  bool loop = false;
  bool stop = false;
};

struct VoiceEvent {
  std::string file;
  std::string path;
  std::string ch;
};

struct MovieRequest {
  std::string file;
  std::string path;
  bool cancellable = true;
  bool active = false;
};

struct QuakeEvent {
  int timeMs = 0;
  int powerX = 0;
  int powerY = 0;
};

struct ChoiceItem {
  std::string text;
  std::string targetFile;
  std::string targetLabel;
  std::string targetBlock;
  std::string exp;
};

struct DialogueState {
  std::string speaker;
  std::string text;
  std::string voiceFile;
  std::string voicePath;
  std::string voiceCh;
  bool hasText = false;
  bool boxVisible = true;
};

struct CallFrame {
  std::string scriptName;
  std::string returnBlock;
};

enum class VmWaitReason {
  None,
  Dialogue,
  Choice,
  WaitTimer,
  Movie,
  TitleReturn,
  Ended,
};

} // namespace Artemis
