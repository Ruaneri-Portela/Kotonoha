// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <Artemis/ArtemisTypes.hpp>
#include <Artemis/AstParser.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Artemis {

// Pure C++17 Artemis Engine script VM (zero SDL dependency).
// Executes parsed .ast Lua-table scripts, maintaining scene state, variables,
// call stack, audio/movie/quake queues, and save/load serialization.
class ArtemisVm {
public:
  using ScriptLoader = std::function<std::string(const std::string &scriptId)>;

  ArtemisVm() = default;

  void setScriptLoader(ScriptLoader loader) { loader_ = std::move(loader); }

  void setPreferredLanguage(std::string lang) {
    preferredLanguage_ = std::move(lang);
  }
  const std::string &preferredLanguage() const { return preferredLanguage_; }
  std::vector<std::string> availableLanguages() const;

  void reset();
  bool loadScript(const std::string &scriptId,
                  const std::string &startLabelOrBlock = {});

  // Steps execution until the VM yields (Dialogue, Choice, WaitTimer, Movie,
  // TitleReturn, Ended) or hits maxSteps.
  VmWaitReason step(int maxSteps = 2048);

  // Player actions to resume from yielded states.
  bool advance();
  bool selectChoice(std::size_t index);
  void finishMovie();
  void finishWaitTimer();

  VmWaitReason waitReason() const { return waitReason_; }
  int waitTimerMs() const { return waitTimerMs_; }

  const std::string &currentScript() const { return currentScript_; }
  const std::string &currentBlock() const { return currentBlock_; }
  const std::string &saveTitle() const { return saveTitle_; }
  const BgState &bg() const { return bg_; }
  const std::vector<SpriteLayer> &sprites() const { return sprites_; }
  const BgmState &bgm() const { return bgm_; }
  const DialogueState &dialogue() const { return dialogue_; }
  const std::vector<ChoiceItem> &choices() const { return choices_; }
  const MovieRequest &movie() const { return movie_; }
  const std::string &colorTone() const { return colorTone_; }

  std::vector<SeEvent> consumeSeEvents();
  std::vector<VoiceEvent> consumeVoiceEvents();
  std::vector<QuakeEvent> consumeQuakeEvents();

  // Variable / expression helpers.
  double getVar(const std::string &name) const;
  void setVar(const std::string &name, double value);
  bool evalCondition(std::string_view expr) const;
  void evalExpression(std::string_view expr);

  // Debug & navigation helpers.
  std::vector<std::string> currentScriptBlocks() const;
  bool jumpToBlock(const std::string &blockName);

  // Deterministic text serialization for save/load.
  std::string serializeState() const;
  bool deserializeState(std::string_view data);

private:
  const ParsedAstScript *getOrLoadScript(const std::string &scriptId);
  const LuaTable *findBlock(const ParsedAstScript &script,
                            const std::string &blockName) const;
  std::string resolveNextBlock(const ParsedAstScript &script,
                               const std::string &blockName,
                               const LuaTable &blockTable) const;
  std::string resolveLabelToBlock(const ParsedAstScript &script,
                                  const std::string &labelOrBlock) const;

  void executeCommand(const LuaTable &cmd, const LuaTable &blockTable,
                      bool &jumped, bool &yielded);
  void extractBlockDialogue(const LuaTable &blockTable);
  void extractBlockChoices(const LuaTable &cmd, const LuaTable &blockTable);
  std::string pickLocalizedText(const LuaTable &langContainer,
                                std::string &outSpeaker) const;

  ScriptLoader loader_;
  std::unordered_map<std::string, ParsedAstScript> scriptCache_;
  std::string preferredLanguage_ = "en";

  std::string currentScript_;
  std::string currentBlock_;
  std::size_t currentCmdIndex_ = 0;
  std::vector<CallFrame> callStack_;

  VmWaitReason waitReason_ = VmWaitReason::None;
  int waitTimerMs_ = 0;

  std::string saveTitle_;
  BgState bg_;
  std::vector<SpriteLayer> sprites_;
  BgmState bgm_;
  DialogueState dialogue_;
  std::vector<ChoiceItem> choices_;
  MovieRequest movie_;
  std::string colorTone_;

  std::vector<SeEvent> pendingSe_;
  std::vector<VoiceEvent> pendingVoice_;
  std::vector<QuakeEvent> pendingQuake_;

  std::map<std::string, double> vars_;
};

} // namespace Artemis
