// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Ponscripter/PonscripterTypes.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ponscripter {

struct CallStackEntry {
  std::size_t returnStatementIndex = 0;
  std::string targetLabel;
};

struct ForLoopFrame {
  int varId = 0;
  int endValue = 0;
  int step = 1;
  std::size_t bodyStatementIndex = 0; // statement right after `for`
};

struct GlobalSaveData {
  std::unordered_map<int, int> globals; // %200 .. %4095
  std::set<std::string> seenFiles;      // fchk
  std::set<std::string> seenLabels;     // lchk
  std::set<std::size_t> kidokuLines;    // read text statement indices
};

struct SlotSaveData {
  SaveTimestamp timestamp;
  std::size_t statementIndex = 0;
  std::string currentLabel;
  std::unordered_map<int, int> locals; // %0 .. %199
  std::vector<CallStackEntry> callStack;
  std::vector<ForLoopFrame> forStack;

  TaggedImageSpec bg;
  std::array<StandingSlot, 3> standing;
  std::map<int, SpriteSlot> sprites;
  WindowSpec window;
  bool textWindowVisible = false;
  std::vector<TextSpan> pageSpans;
  std::string pagePlainText;

  std::string activeBgm;
  bool activeBgmLoop = false;
  std::map<int, std::string> loopingWaves;

  WaitState waitState = WaitState::WaitClickInline;
  int btnwaitDestVar = -1;
  bool btnwaitPreserveButtons = false;
  std::vector<ButtonDef> buttons;
  std::vector<SelectOption> selectOptions;
};

class SaveSerializer {
public:
  static std::string serializeGlobals(const GlobalSaveData &data);
  static bool deserializeGlobals(std::string_view text, GlobalSaveData &outData);

  static std::string serializeSlot(const SlotSaveData &data);
  static bool deserializeSlot(std::string_view text, SlotSaveData &outData);
  static SaveTimestamp peekSlotTimestamp(std::string_view text);
};

} // namespace Ponscripter
