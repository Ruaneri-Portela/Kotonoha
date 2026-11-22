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

struct CallArgument {
  bool isString = false;
  int number = 0;
  std::string text;
  int sourceVariable = -1; // source variable id for getparam i/s indirection
  bool sourceIsString = false;
};

struct CallStackEntry {
  std::size_t returnStatementIndex = 0;
  std::string targetLabel;
  std::string callerLabel; // restore source scene after a gosub return
  std::vector<CallArgument> arguments;
  std::size_t nextArgument = 0;
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
  bool allSpritesHidden = false;
  bool allSprites2Hidden = false;
  bool manualFrameVisible = false;
  std::vector<ManualDrawOp> manualDraftOps;
  std::vector<ManualDrawOp> manualDisplayOps;
  std::vector<ManualDrawOp> backgroundCopyOps;
  ActiveEffect activeEffect;
  int quakeAmplitude = 0;
  bool monochromeEnabled = false;
  std::uint32_t monochromeColor = 0xFFFFFFu;
  int negativeMode = 0;
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
  std::vector<SelectOption> cselOptions;
  std::size_t pendingPretextStatementIndex = static_cast<std::size_t>(-1);
  bool textGosubPagePending = false;
  // Optional metadata, so V1 slot files remain readable without this line.
  int selectionMode = 0; // 0=select/csel, 1=selgosub, 2=selnum
  int selectionNumberVariable = -1;
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
