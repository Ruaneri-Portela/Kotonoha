// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Ponscripter/Lexer.hpp>
#include <Ponscripter/PonscripterTypes.hpp>
#include <Ponscripter/SaveSerializer.hpp>
#include <Ponscripter/ScriptBuffer.hpp>
#include <Ponscripter/TextFormatter.hpp>
#include <Ponscripter/VariableStore.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Ponscripter {

class PonscripterVm {
public:
  static constexpr int kMaxExtendedSprites = 1000;
  static constexpr int kMaxBars = 16;
  static constexpr int kMaxPrnums = 16;

  explicit PonscripterVm(HostCallbacks *callbacks = nullptr);

  void setCallbacks(HostCallbacks *callbacks) { callbacks_ = callbacks; }
  HostCallbacks *callbacks() const { return callbacks_; }

  // Script loading & lifecycle
  void clear();
  void appendScriptFile(std::string_view fileName, std::string_view utf8Content);
  // Runs *define block and jumps to *start, ready to execute.
  bool start();
  // Resets local state/scene while preserving globals, then jumps to *start.
  void resetToStart();
  bool jumpToLabel(std::string_view label);

  // Main execution step / frame tick
  // Advances timers, transitions, multi-cell animations, and executes up to
  // `maxInstructions` while in WaitState::Running.
  void tick(double dtMs, std::size_t maxInstructions = 4096);

  // Execute one statement immediately (used by tests and tick()).
  bool stepStatement();

  // User / Host interaction events
  void updateMousePos(int x, int y);
  // Left-click / Enter / Advance action
  bool advance();
  // Right-click / Escape action
  bool rightClick();
  // Click at logical (x, y) in viewport coordinates (0..width, 0..height)
  bool clickAt(int x, int y, bool isRightClick = false);
  // Directly trigger a button ID when in WaitButton (for programmatic tests / UI)
  bool selectButton(int buttonId);
  // Directly select an option index (0-based) when in WaitSelect
  bool selectChoice(int optionIndex);
  // Complete an active systemcall (save, load, rmenu, lookback, windowerase)
  void finishSystemCall();
  // Enter a systemcall directly (e.g. from rmenu selection)
  void enterSystemCall(const std::string &kind);
  // Complete an active video playback (mpegplay / avi)
  void finishVideo();

  void setSkipMode(bool enabled);
  bool isSkipMode() const { return skipMode_; }
  void setAutoMode(bool enabled);
  bool isAutoMode() const { return autoMode_; }

  // Inspection getters
  WaitState waitState() const { return waitState_; }
  bool isHalted() const { return waitState_ == WaitState::Halted; }
  Resolution resolution() const { return script_.directives().resolution; }
  std::size_t currentStatementIndex() const { return pc_; }
  const std::string &currentLabel() const { return currentLabel_; }
  const std::string &caption() const { return caption_; }
  const std::string &versionStr1() const { return versionStr1_; }
  const std::string &versionStr2() const { return versionStr2_; }
  bool useNsaArchive() const { return useNsa_; }
  bool isEffectCutEnabled() const { return effectCut_; }
  bool isRightClickEnabled() const { return rmode_; }

  VariableStore &variables() { return vars_; }
  const VariableStore &variables() const { return vars_; }
  const ScriptBuffer &script() const { return script_; }

  // Scene & Visual State
  const TaggedImageSpec &background() const { return bg_; }
  const std::array<StandingSlot, 3> &standingSlots() const { return standing_; }
  const std::array<SpriteSlot, kMaxExtendedSprites> &sprites() const {
    return sprites_;
  }
  bool areSpritesHidden(bool secondaryLayer) const {
    return secondaryLayer ? allSprites2Hidden_ : allSpritesHidden_;
  }
  SpriteSlot *mutableSprite(int id) {
    return (id >= 0 && id < kMaxExtendedSprites) ? &sprites_[id] : nullptr;
  }
  const std::array<BarGauge, kMaxBars> &bars() const { return bars_; }
  const std::array<PrintedNumber, kMaxPrnums> &printedNumbers() const {
    return prnums_;
  }
  const TaggedImageSpec &btndefSpec() const { return btndefSpec_; }
  bool hasManualDrawFrame() const { return manualFrameVisible_; }
  const std::vector<ManualDrawOp> &manualDrawFrame() const {
    return manualDisplayOps_;
  }
  const std::vector<ButtonDef> &activeButtons() const { return buttons_; }
  int hoveredButtonId() const { return hoveredButtonId_; }
  int mouseX() const { return mouseX_; }
  int mouseY() const { return mouseY_; }

  const ActiveEffect &activeEffect() const { return activeEffect_; }
  std::pair<int, int> quakeOffset() const;
  bool isMonochromeEnabled() const { return monochromeEnabled_; }
  std::uint32_t monochromeColor() const { return monochromeColor_; }
  int negativeMode() const { return negativeMode_; }
  const EffectDef &windowEffect() const { return windowEffect_; }

  // Text & Window State
  const WindowSpec &windowSpec() const { return window_; }
  bool isTextWindowVisible() const { return textWindowVisible_; }
  const std::vector<TextSpan> &currentPageSpans() const { return pageSpans_; }
  const std::string &currentPagePlainText() const { return pagePlainText_; }
  const std::vector<std::string> &backlogPages() const { return backlog_; }
  std::size_t typewriterCharIndex() const { return typewriterChars_; }
  std::size_t totalPageChars() const { return totalPageChars_; }
  bool isTypewriterActive() const {
    return typewriterChars_ < totalPageChars_;
  }
  const std::array<CursorSpec, 2> &cursors() const { return cursors_; }
  const std::array<TaggedImageSpec, 4> &lookbackButtons() const {
    return lookbackButtons_;
  }

  // Font mapping (h_mapfont / pmapfont)
  const std::string &mappedFont(int slot) const;

  // Choice & Menu State
  const std::vector<SelectOption> &selectOptions() const {
    return selectOptions_;
  }
  std::uint32_t selectHoverColor() const { return selectHoverColor_; }
  std::uint32_t selectIdleColor() const { return selectIdleColor_; }
  const std::vector<RMenuEntry> &rmenuEntries() const { return rmenuEntries_; }
  std::uint32_t rmenuHoverColor() const { return rmenuHoverColor_; }
  std::uint32_t rmenuIdleColor() const { return rmenuIdleColor_; }
  const std::string &activeSystemCall() const { return activeSystemCall_; }
  const std::string &saveMenuTitle() const { return saveMenuTitle_; }
  const std::string &loadMenuTitle() const { return loadMenuTitle_; }
  const std::string &saveSlotPrefix() const { return saveSlotPrefix_; }
  int saveNumberCount() const { return saveNumberCount_; }

  // Audio State
  const std::string &activeBgm() const { return activeBgm_; }
  bool isActiveBgmLooping() const { return activeBgmLoop_; }
  int bgmVolume() const { return bgmVol_; }
  int seVolume() const { return seVol_; }
  int voiceVolume() const { return voiceVol_; }
  const std::map<int, std::string> &loopingWaves() const {
    return loopingWaves_;
  }
  const std::string &lastVoicePath() const { return lastVoicePath_; }
  const std::string &lastSePath() const { return lastSePath_; }
  const std::vector<std::string> &currentPageLineKeys() const {
    return pageLineKeys_;
  }
  std::size_t currentSectionId() const { return currentSectionId_; }

  // Save / Load State Serialization
  GlobalSaveData exportGlobalSave() const;
  void importGlobalSave(const GlobalSaveData &data);
  SlotSaveData exportSlotSave() const;
  void importSlotSave(const SlotSaveData &data);
  void clearCurrentPage(bool pushBacklog);

private:
  using CommandHandler = void (PonscripterVm::*)(LineLexer &lexer,
                                                 const std::string &cmd);

  void registerCommands();
  void registerCommand(std::string_view name, CommandHandler handler);

  void executeStatement(const StatementSlice &stmt);
  void executeTextLine(std::string_view rawLine, std::size_t stmtIndex);
  void captureCallArguments(LineLexer &lexer, CallStackEntry &frame,
                            bool requireLeadingComma);

  void enterTextDisplayMode();
  void leaveTextDisplayMode(bool forceHide = false);

  EffectDef resolveEffectSpec(LineLexer &lexer, bool hasLeadingComma = true);
  void triggerEffect(const EffectDef &eff);
  void populateSpriteDimensions(SpriteSlot &slot);
  void populateStandingDimensions(StandingSlot &slot, int slotIndex);
  void updateButtonHover();

  // Command categories (implemented in PonscripterCommands.cpp)
  void cmdLifecycle(LineLexer &lexer, const std::string &cmd);
  void cmdAlias(LineLexer &lexer, const std::string &cmd);
  void cmdFlow(LineLexer &lexer, const std::string &cmd);
  void cmdVars(LineLexer &lexer, const std::string &cmd);
  void cmdGraphics(LineLexer &lexer, const std::string &cmd);
  void cmdEffects(LineLexer &lexer, const std::string &cmd);
  void cmdTextAndWindow(LineLexer &lexer, const std::string &cmd);
  void cmdButtonsAndInput(LineLexer &lexer, const std::string &cmd);
  void cmdAudioAndVideo(LineLexer &lexer, const std::string &cmd);
  void cmdMenusAndSystem(LineLexer &lexer, const std::string &cmd);
  void cmdNoOp(LineLexer &lexer, const std::string &cmd);

  HostCallbacks *callbacks_ = nullptr;
  ScriptBuffer script_;
  VariableStore vars_;
  std::unordered_map<std::string, CommandHandler> funcLut_;
  std::unordered_map<std::string, std::string> userDefsubs_;

  std::size_t pc_ = 0;
  std::string currentLabel_;
  WaitState waitState_ = WaitState::Running;
  bool inDefineBlock_ = true;

  // Call & loop stacks
  std::vector<CallStackEntry> callStack_;
  std::vector<ForLoopFrame> forStack_;

  // Configuration & metadata
  bool useNsa_ = false;
  bool effectCut_ = false;
  bool rmode_ = true;
  bool skipMode_ = false;
  bool autoMode_ = false;
  int autoModeTimeMs_ = 1000;
  double autoWaitAccumMs_ = 0.0;
  std::string caption_ = "Ponscripter";
  std::string versionStr1_;
  std::string versionStr2_;
  std::array<std::string, 8> fontMap_;
  std::string emptyFont_;

  // Timer state
  double waitTargetMs_ = 0.0;
  double waitElapsedMs_ = 0.0;
  bool waitSkippable_ = true;

  // Effects
  std::unordered_map<int, EffectDef> userEffects_;
  EffectDef windowEffect_{0, 1, 0, ""};
  ActiveEffect activeEffect_;
  int quakeAmplitude_ = 0;
  bool monochromeEnabled_ = false;
  std::uint32_t monochromeColor_ = 0xFFFFFFu;
  int negativeMode_ = 0;

  // Visual layers
  int underlineValue_ = 599;
  TaggedImageSpec bg_;
  std::array<StandingSlot, 3> standing_;
  std::array<SpriteSlot, kMaxExtendedSprites> sprites_;
  bool allSpritesHidden_ = false;
  bool allSprites2Hidden_ = false;
  bool manualFrameVisible_ = false;
  std::vector<ManualDrawOp> manualDraftOps_;
  std::vector<ManualDrawOp> manualDisplayOps_;
  std::array<BarGauge, kMaxBars> bars_;
  std::array<PrintedNumber, kMaxPrnums> prnums_;

  // Buttons & mouse
  TaggedImageSpec btndefSpec_;
  std::vector<ButtonDef> buttons_;
  int btnwaitDestVar_ = -1;
  bool btnwaitPreserveButtons_ = false;
  int hoveredButtonId_ = 0;
  int mouseX_ = 0;
  int mouseY_ = 0;
  int lastClickX_ = 0;
  int lastClickY_ = 0;

  // Text & Window
  WindowSpec window_;
  bool textWindowVisible_ = false;
  bool windowback_ = false;
  TextStyle currentTextStyle_;
  std::vector<TextSpan> pageSpans_;
  std::vector<std::string> pageLineKeys_;
  std::string pagePlainText_;
  std::size_t typewriterChars_ = 0;
  std::size_t totalPageChars_ = 0;
  double typewriterAccumMs_ = 0.0;
  std::vector<std::string> backlog_;
  // Script-level text callbacks are invoked as gosubs and return to the
  // original script cursor. A pending pretext index avoids re-entry loops.
  std::string pretextGosubLabel_;
  std::string textGosubLabel_;
  std::size_t pendingPretextPc_ = ScriptBuffer::npos;
  bool textGosubPagePending_ = false;
  std::array<CursorSpec, 2> cursors_;
  std::array<TaggedImageSpec, 4> lookbackButtons_;
  std::size_t currentSectionId_ = 0;

  // Select / Csel / RMenu / Save config
  enum class SelectionMode : std::uint8_t {
    Jump = 0,
    Subroutine = 1,
    Number = 2
  };
  SelectionMode selectionMode_ = SelectionMode::Jump;
  int selectionNumberVariable_ = -1;
  std::vector<SelectOption> selectOptions_;
  std::vector<SelectOption> cselOptions_;
  std::uint32_t selectHoverColor_ = 0xFF0000u;
  std::uint32_t selectIdleColor_ = 0xFFFFFFu;
  std::vector<RMenuEntry> rmenuEntries_;
  std::uint32_t rmenuHoverColor_ = 0xFF9999u;
  std::uint32_t rmenuIdleColor_ = 0x99FF99u;
  std::uint32_t rmenuDisabledColor_ = 0xFFFFFFu;
  std::string activeSystemCall_;
  WaitState preSystemCallWaitState_ = WaitState::Running;
  std::string saveMenuTitle_ = "~ Save ~";
  std::string loadMenuTitle_ = "~ Load ~";
  std::string saveSlotPrefix_ = "#";
  int saveNumberCount_ = 20;

  // Audio state
  std::string activeBgm_;
  bool activeBgmLoop_ = false;
  int bgmVol_ = 100;
  int seVol_ = 100;
  int voiceVol_ = 100;
  std::map<int, std::string> loopingWaves_;
  std::string lastVoicePath_;
  std::string lastSePath_;

  // Seen tracking (fchk / lchk / kidoku)
  std::set<std::string> seenFiles_;
  std::set<std::string> seenLabels_;
  std::set<std::size_t> kidokuLines_;
};

} // namespace Ponscripter
