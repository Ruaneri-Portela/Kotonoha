// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <Artemis/ArtemisVm.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace Artemis {
namespace {

std::string trim(std::string_view sv) {
  std::size_t b = 0;
  while (b < sv.size() && std::isspace(static_cast<unsigned char>(sv[b])))
    ++b;
  std::size_t e = sv.size();
  while (e > b && std::isspace(static_cast<unsigned char>(sv[e - 1])))
    --e;
  return std::string(sv.substr(b, e - b));
}

std::string toLowerAscii(std::string_view sv) {
  std::string out(sv);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

bool isPlaceholderText(std::string_view text) {
  const std::string t = trim(text);
  return t.empty() || t == "*テキスト*" || t == "*text*";
}

// Extracts text and optional speaker from one language's text array table.
// Example: { { name = {"淳之介", "Junnosuke"}, "“Hello.”", {"rt2"} } }
void collectLanguageLines(const LuaTable &langTable, std::string &outText,
                          std::string &outSpeaker) {
  auto processItem = [&](const LuaValue &item, auto &&self) -> void {
    if (item.isString()) {
      outText += item.strVal;
      return;
    }
    if (!item.isTable())
      return;
    const LuaTable &tbl = *item.tableVal;

    // Check for speaker `name = {"JP", "EN"}` or `name = "Speaker"`.
    if (const auto *nameVal = tbl.get("name")) {
      if (nameVal->isTable() && !nameVal->tableVal->array.empty()) {
        // Prefer localized second entry if present, otherwise first entry.
        const auto &arr = nameVal->tableVal->array;
        if (arr.size() >= 2 && !arr[1].asString().empty())
          outSpeaker = arr[1].asString();
        else
          outSpeaker = arr[0].asString();
      } else if (nameVal->isString()) {
        outSpeaker = nameVal->strVal;
      }
    }

    // Inline control tag like {"rt2"}, {"ruby", text="..."}, {"tx", ...}.
    if (!tbl.array.empty() && tbl.array[0].isString()) {
      const std::string &tag = tbl.array[0].strVal;
      if (tag == "rt2" || tag == "rt" || tag == "br") {
        if (!outText.empty() && outText.back() != '\n')
          outText.push_back('\n');
        return;
      }
      if (tag == "ruby") {
        // Skip ruby furigana annotation in plain dialogue text.
        return;
      }
      if (tag == "tx" || tag == "exfont") {
        if (tbl.has("text"))
          outText += tbl.getString("text");
        return;
      }
    }

    // Recurse into array items.
    for (const auto &child : tbl.array) {
      self(child, self);
    }
  };

  for (const auto &entry : langTable.array) {
    processItem(entry, processItem);
  }

  // Trim trailing newlines.
  while (!outText.empty() && (outText.back() == '\n' || outText.back() == '\r'))
    outText.pop_back();
}

std::string escapeField(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '|')
      out += "\\p";
    else
      out.push_back(c);
  }
  return out;
}

std::string unescapeField(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      if (n == 'n')
        out.push_back('\n');
      else if (n == 'r')
        out.push_back('\r');
      else if (n == 'p')
        out.push_back('|');
      else
        out.push_back(n);
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::vector<std::string> splitPipe(std::string_view line) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto bar = line.find('|', start);
    if (bar == std::string_view::npos) {
      parts.push_back(unescapeField(line.substr(start)));
      break;
    }
    parts.push_back(unescapeField(line.substr(start, bar - start)));
    start = bar + 1;
  }
  return parts;
}

} // namespace

void ArtemisVm::reset() {
  currentScript_.clear();
  currentBlock_.clear();
  currentCmdIndex_ = 0;
  callStack_.clear();
  waitReason_ = VmWaitReason::None;
  waitTimerMs_ = 0;
  saveTitle_.clear();
  bg_ = BgState{};
  sprites_.clear();
  bgm_ = BgmState{};
  dialogue_ = DialogueState{};
  choices_.clear();
  movie_ = MovieRequest{};
  colorTone_.clear();
  pendingSe_.clear();
  pendingVoice_.clear();
  pendingQuake_.clear();
  vars_.clear();
}

const ParsedAstScript *ArtemisVm::getOrLoadScript(const std::string &scriptId) {
  auto it = scriptCache_.find(scriptId);
  if (it != scriptCache_.end())
    return &it->second;
  if (!loader_)
    return nullptr;
  const std::string src = loader_(scriptId);
  if (src.empty())
    return nullptr;
  auto [inserted, _] =
      scriptCache_.emplace(scriptId, AstParser::parseScript(src));
  return &inserted->second;
}

const LuaTable *ArtemisVm::findBlock(const ParsedAstScript &script,
                                     const std::string &blockName) const {
  if (!script.ast)
    return nullptr;
  return script.ast->getTable(blockName);
}

std::string
ArtemisVm::resolveLabelToBlock(const ParsedAstScript &script,
                               const std::string &labelOrBlock) const {
  if (!script.ast)
    return {};
  if (labelOrBlock.empty()) {
    if (script.label) {
      if (const LuaTable *top = script.label->getTable("top")) {
        const std::string blk = top->getString("block");
        if (!blk.empty() && script.ast->getTable(blk))
          return blk;
      }
    }
    return script.blockOrder.empty() ? std::string{}
                                     : script.blockOrder.front();
  }
  if (script.ast->getTable(labelOrBlock))
    return labelOrBlock;
  if (script.label) {
    if (const LuaTable *lbl = script.label->getTable(labelOrBlock)) {
      const std::string blk = lbl->getString("block");
      if (!blk.empty() && script.ast->getTable(blk))
        return blk;
    }
  }
  return script.blockOrder.empty() ? std::string{} : script.blockOrder.front();
}

std::string ArtemisVm::resolveNextBlock(const ParsedAstScript &script,
                                        const std::string &blockName,
                                        const LuaTable &blockTable) const {
  if (blockTable.has("linknext")) {
    const std::string next = blockTable.getString("linknext");
    if (!next.empty() && script.ast && script.ast->getTable(next))
      return next;
  }
  for (std::size_t i = 0; i + 1 < script.blockOrder.size(); ++i) {
    if (script.blockOrder[i] == blockName)
      return script.blockOrder[i + 1];
  }
  return {};
}

bool ArtemisVm::loadScript(const std::string &scriptId,
                           const std::string &startLabelOrBlock) {
  const ParsedAstScript *script = getOrLoadScript(scriptId);
  if (!script || !script->ast)
    return false;
  currentScript_ = scriptId;
  currentBlock_ = resolveLabelToBlock(*script, startLabelOrBlock);
  currentCmdIndex_ = 0;
  waitReason_ = VmWaitReason::None;
  waitTimerMs_ = 0;
  choices_.clear();
  movie_.active = false;
  return !currentBlock_.empty();
}

std::vector<std::string> ArtemisVm::availableLanguages() const {
  std::vector<std::string> langs;
  auto it = scriptCache_.find(currentScript_);
  if (it == scriptCache_.end() || !it->second.ast)
    return langs;
  for (const auto &blkName : it->second.blockOrder) {
    const LuaTable *blk = it->second.ast->getTable(blkName);
    if (!blk)
      continue;
    const LuaTable *textTbl = blk->getTable("text");
    if (!textTbl)
      continue;
    for (const auto &[k, v] : textTbl->fields) {
      if (v.isTable() &&
          std::find(langs.begin(), langs.end(), k) == langs.end()) {
        langs.push_back(k);
      }
    }
    if (!langs.empty())
      break;
  }
  return langs;
}

std::string ArtemisVm::pickLocalizedText(const LuaTable &textTbl,
                                         std::string &outSpeaker) const {
  std::vector<std::string> order;
  if (!preferredLanguage_.empty())
    order.push_back(preferredLanguage_);
  for (const char *fb : {"en", "ja", "cn", "tw"}) {
    if (std::find(order.begin(), order.end(), fb) == order.end())
      order.push_back(fb);
  }
  for (const auto &[k, _] : textTbl.fields) {
    if (std::find(order.begin(), order.end(), k) == order.end())
      order.push_back(k);
  }

  std::string fallbackText;
  std::string fallbackSpeaker;
  for (const auto &lang : order) {
    const LuaTable *langTbl = textTbl.getTable(lang);
    if (!langTbl)
      continue;
    std::string text;
    std::string speaker;
    collectLanguageLines(*langTbl, text, speaker);
    if (!isPlaceholderText(text)) {
      outSpeaker = std::move(speaker);
      return text;
    }
    if (fallbackText.empty() && !text.empty()) {
      fallbackText = std::move(text);
      fallbackSpeaker = std::move(speaker);
    }
  }

  // Handle legacy single-language `text = { { "Line" } }` format.
  if (!textTbl.array.empty()) {
    std::string text;
    std::string speaker;
    collectLanguageLines(textTbl, text, speaker);
    if (!text.empty()) {
      outSpeaker = std::move(speaker);
      return text;
    }
  }

  outSpeaker = std::move(fallbackSpeaker);
  return fallbackText;
}

void ArtemisVm::extractBlockDialogue(const LuaTable &blockTable) {
  const LuaTable *textTbl = blockTable.getTable("text");
  if (!textTbl)
    return;
  std::string speaker;
  std::string text = pickLocalizedText(*textTbl, speaker);
  if (text.empty())
    return;
  dialogue_.speaker = std::move(speaker);
  dialogue_.text = std::move(text);
  dialogue_.hasText = true;
  dialogue_.boxVisible = true;
}

void ArtemisVm::extractBlockChoices(const LuaTable &cmd,
                                    const LuaTable &blockTable) {
  choices_.clear();
  const LuaTable *selTbl = blockTable.getTable("select");
  if (!selTbl)
    selTbl = &cmd;

  // Try localized subtable first (select = { en = {...}, ja = {...} }).
  const LuaTable *langTbl = nullptr;
  if (selTbl->getTable(preferredLanguage_))
    langTbl = selTbl->getTable(preferredLanguage_);
  else if (selTbl->getTable("en"))
    langTbl = selTbl->getTable("en");
  else if (selTbl->getTable("ja"))
    langTbl = selTbl->getTable("ja");

  auto parseChoicesFromArray = [&](const LuaTable &tbl) {
    for (const auto &entry : tbl.array) {
      if (entry.isString()) {
        ChoiceItem item;
        item.text = entry.strVal;
        choices_.push_back(std::move(item));
      } else if (entry.isTable()) {
        const LuaTable &t = *entry.tableVal;
        if (t.has("cond") && !evalCondition(t.getString("cond")))
          continue;
        ChoiceItem item;
        item.text = t.getString("text");
        if (item.text.empty() && !t.array.empty())
          item.text = t.array[0].asString();
        item.targetFile = t.getString("file");
        item.targetLabel = t.getString("label");
        item.targetBlock = t.getString("block");
        item.exp = t.getString("exp");
        if (!item.text.empty())
          choices_.push_back(std::move(item));
      }
    }
  };

  if (langTbl) {
    parseChoicesFromArray(*langTbl);
  } else {
    parseChoicesFromArray(*selTbl);
  }

  // Also check `link = { { file=..., label=... }, ... }` paired with choice
  // texts.
  if (const LuaTable *linkTbl = blockTable.getTable("link")) {
    for (std::size_t i = 0; i < linkTbl->array.size() && i < choices_.size();
         ++i) {
      if (!linkTbl->array[i].isTable())
        continue;
      const LuaTable &lt = *linkTbl->array[i].tableVal;
      if (choices_[i].targetFile.empty())
        choices_[i].targetFile = lt.getString("file");
      if (choices_[i].targetLabel.empty())
        choices_[i].targetLabel = lt.getString("label");
      if (choices_[i].targetBlock.empty())
        choices_[i].targetBlock = lt.getString("block");
      if (choices_[i].exp.empty())
        choices_[i].exp = lt.getString("exp");
    }
  }
}

void ArtemisVm::executeCommand(const LuaTable &cmd, const LuaTable &blockTable,
                               bool &jumped, bool &yielded) {
  if (cmd.array.empty())
    return;
  const std::string tag = cmd.array[0].asString();
  if (tag.empty())
    return;

  if (cmd.has("cond") && !evalCondition(cmd.getString("cond")))
    return;

  if (tag == "savetitle") {
    saveTitle_ = cmd.getString("text");
    return;
  }

  if (tag == "eval") {
    evalExpression(cmd.getString("exp"));
    return;
  }

  if (tag == "bg") {
    const std::string file = cmd.getString("file");
    const std::string lowerFile = toLowerAscii(file);
    bg_.file = file;
    bg_.path = cmd.getString("path", ":bg/");
    bg_.x = cmd.getInt("x", 0);
    bg_.y = cmd.getInt("y", 0);
    bg_.ax = cmd.getInt("ax", 0);
    bg_.ay = cmd.getInt("ay", 0);
    bg_.bx = cmd.getInt("bx", 0);
    bg_.by = cmd.getInt("by", 0);
    bg_.zoom = cmd.getInt("zoom", 100);
    bg_.alpha = cmd.getInt("alpha", 255);
    bg_.transitionMs = cmd.getInt("time", 0);
    bg_.rule = cmd.getString("rule");
    if (lowerFile == "black" || lowerFile == "blackback") {
      bg_.isSolidColor = true;
      bg_.solidR = bg_.solidG = bg_.solidB = 0;
    } else if (lowerFile == "white" || lowerFile == "whiteback") {
      bg_.isSolidColor = true;
      bg_.solidR = bg_.solidG = bg_.solidB = 255;
    } else {
      bg_.isSolidColor = false;
    }
    return;
  }

  if (tag == "fg" || tag == "fgf") {
    const int mode = cmd.getInt("mode", 1);
    const int id = cmd.getInt("id", 0);
    const std::string ch = cmd.getString("ch");
    if (mode == -2) {
      sprites_.clear();
      return;
    }
    if (mode == -1) {
      sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(),
                                    [&](const SpriteLayer &s) {
                                      if (id != 0)
                                        return s.id == id;
                                      if (!ch.empty())
                                        return s.ch == ch;
                                      return true;
                                    }),
                     sprites_.end());
      return;
    }
    // Upsert sprite layer by id (or ch if id == 0).
    SpriteLayer *target = nullptr;
    for (auto &s : sprites_) {
      if ((id != 0 && s.id == id) || (id == 0 && !ch.empty() && s.ch == ch)) {
        target = &s;
        break;
      }
    }
    if (!target) {
      sprites_.push_back(SpriteLayer{});
      target = &sprites_.back();
      target->id = id;
    }
    if (cmd.has("lv"))
      target->lv = cmd.getInt("lv", 5);
    if (!ch.empty())
      target->ch = ch;
    if (cmd.has("path"))
      target->path = cmd.getString("path");
    if (cmd.has("file"))
      target->file = cmd.getString("file");
    if (cmd.has("file1"))
      target->file1 = cmd.getString("file1");
    else if (cmd.has("file"))
      target->file1.clear();
    if (cmd.has("file2"))
      target->file2 = cmd.getString("file2");
    else if (cmd.has("file"))
      target->file2.clear();
    if (cmd.has("x"))
      target->x = cmd.getInt("x", 0);
    if (cmd.has("y"))
      target->y = cmd.getInt("y", 0);
    if (cmd.has("ax"))
      target->ax = cmd.getInt("ax", 0);
    if (cmd.has("ay"))
      target->ay = cmd.getInt("ay", 0);
    if (cmd.has("bx"))
      target->bx = cmd.getInt("bx", 0);
    if (cmd.has("by"))
      target->by = cmd.getInt("by", 0);
    if (cmd.has("alpha"))
      target->alpha = cmd.getInt("alpha", 255);
    if (cmd.has("zoom"))
      target->zoom = cmd.getInt("zoom", 100);
    target->visible = true;
    std::stable_sort(
        sprites_.begin(), sprites_.end(),
        [](const SpriteLayer &a, const SpriteLayer &b) { return a.lv < b.lv; });
    return;
  }

  if (tag == "cgdel") {
    const int id = cmd.getInt("id", -1);
    if (id < 0) {
      sprites_.clear();
    } else {
      sprites_.erase(
          std::remove_if(sprites_.begin(), sprites_.end(),
                         [id](const SpriteLayer &s) { return s.id == id; }),
          sprites_.end());
    }
    return;
  }

  if (tag == "fgact" || tag == "cgact") {
    const int id = cmd.getInt("id", 0);
    const std::string ch = cmd.getString("ch");
    for (auto &s : sprites_) {
      if ((id != 0 && s.id == id) || (!ch.empty() && s.ch == ch)) {
        if (cmd.has("x"))
          s.x = cmd.getInt("x", s.x);
        if (cmd.has("y"))
          s.y = cmd.getInt("y", s.y);
        if (cmd.has("alpha"))
          s.alpha = cmd.getInt("alpha", s.alpha);
        else if (cmd.has("a2"))
          s.alpha = cmd.getInt("a2", s.alpha);
        if (cmd.has("zoom"))
          s.zoom = cmd.getInt("zoom", s.zoom);
      }
    }
    return;
  }

  if (tag == "extrans" || tag == "transitiontime") {
    bg_.transitionMs = cmd.getInt("time", bg_.transitionMs);
    if (cmd.has("rule"))
      bg_.rule = cmd.getString("rule");
    return;
  }

  if (tag == "msgoff") {
    dialogue_.boxVisible = false;
    return;
  }

  if (tag == "msgon") {
    dialogue_.boxVisible = true;
    return;
  }

  if (tag == "colortone") {
    colorTone_ = cmd.getString("mode");
    return;
  }

  if (tag == "bgm") {
    if (cmd.getInt("stop", 0) != 0) {
      bgm_.playing = false;
      bgm_.file.clear();
      ++bgm_.generation;
    } else if (cmd.has("file")) {
      bgm_.file = cmd.getString("file");
      bgm_.volume = cmd.getInt("vol", 100);
      bgm_.playing = !bgm_.file.empty();
      ++bgm_.generation;
    }
    return;
  }

  if (tag == "se") {
    SeEvent ev;
    ev.id = cmd.getInt("id", 0);
    ev.file = cmd.getString("file");
    ev.volume = cmd.getInt("vol", 100);
    ev.loop = cmd.getInt("loop", 0) != 0;
    ev.stop = cmd.getInt("stop", 0) != 0 || ev.file.empty();
    pendingSe_.push_back(std::move(ev));
    return;
  }

  if (tag == "vo") {
    VoiceEvent ev;
    ev.file = cmd.getString("file");
    ev.path = cmd.getString("path", ":vo/");
    ev.ch = cmd.getString("ch");
    if (!ev.file.empty()) {
      dialogue_.voiceFile = ev.file;
      dialogue_.voicePath = ev.path;
      dialogue_.voiceCh = ev.ch;
      pendingVoice_.push_back(std::move(ev));
    }
    return;
  }

  if (tag == "quake") {
    QuakeEvent ev;
    ev.timeMs = cmd.getInt("time", 400);
    ev.powerX = cmd.getInt("size", cmd.getInt("x", 10));
    ev.powerY = cmd.getInt("size", cmd.getInt("y", 10));
    pendingQuake_.push_back(ev);
    return;
  }

  if (tag == "movie") {
    const std::string file = cmd.getString("file");
    if (!file.empty()) {
      movie_.file = file;
      movie_.path = cmd.getString("path", ":mv/");
      movie_.cancellable = cmd.getInt("cancel", 1) != 0;
      movie_.active = true;
      waitReason_ = VmWaitReason::Movie;
      yielded = true;
    }
    return;
  }

  if (tag == "ex") {
    const std::string func = cmd.getString("func");
    if (func == "wait") {
      const int ms = cmd.getInt("time", 0);
      if (ms > 0) {
        waitTimerMs_ = ms;
        waitReason_ = VmWaitReason::WaitTimer;
        yielded = true;
      }
    }
    return;
  }

  if (tag == "excall" || tag == "call" || tag == "jump") {
    const std::string file = cmd.getString("file");
    const std::string label = cmd.getString("label", cmd.getString("block"));
    const std::string targetScript = file.empty() ? currentScript_ : file;
    const ParsedAstScript *nextScript = getOrLoadScript(targetScript);
    if (!nextScript || !nextScript->ast)
      return;

    if (tag == "excall" || tag == "call") {
      // Save return point to next block in current script.
      if (const ParsedAstScript *curScript = getOrLoadScript(currentScript_)) {
        const LuaTable *curBlk = findBlock(*curScript, currentBlock_);
        std::string retBlk =
            curBlk ? resolveNextBlock(*curScript, currentBlock_, *curBlk)
                   : std::string{};
        if (!retBlk.empty()) {
          callStack_.push_back(CallFrame{currentScript_, std::move(retBlk)});
        }
      }
    }

    currentScript_ = targetScript;
    currentBlock_ = resolveLabelToBlock(*nextScript, label);
    currentCmdIndex_ = 0;
    jumped = true;
    return;
  }

  if (tag == "exreturn" || tag == "return") {
    if (!callStack_.empty()) {
      const CallFrame frame = callStack_.back();
      callStack_.pop_back();
      currentScript_ = frame.scriptName;
      currentBlock_ = frame.returnBlock;
      currentCmdIndex_ = 0;
      jumped = true;
    } else {
      waitReason_ = VmWaitReason::Ended;
      yielded = true;
    }
    return;
  }

  if (tag == "select") {
    extractBlockChoices(cmd, blockTable);
    if (!choices_.empty()) {
      waitReason_ = VmWaitReason::Choice;
      yielded = true;
    }
    return;
  }

  if (tag == "text") {
    dialogue_.voiceFile.clear();
    dialogue_.voicePath.clear();
    dialogue_.voiceCh.clear();
    extractBlockDialogue(blockTable);
    if (dialogue_.hasText && !dialogue_.text.empty()) {
      waitReason_ = VmWaitReason::Dialogue;
      yielded = true;
    }
    return;
  }

  if (tag == "タイトル") {
    waitReason_ = VmWaitReason::TitleReturn;
    yielded = true;
    return;
  }
}

VmWaitReason ArtemisVm::step(int maxSteps) {
  if (waitReason_ == VmWaitReason::Dialogue ||
      waitReason_ == VmWaitReason::Choice ||
      waitReason_ == VmWaitReason::WaitTimer ||
      waitReason_ == VmWaitReason::Movie ||
      waitReason_ == VmWaitReason::TitleReturn ||
      waitReason_ == VmWaitReason::Ended) {
    return waitReason_;
  }

  for (int steps = 0; steps < maxSteps; ++steps) {
    const ParsedAstScript *script = getOrLoadScript(currentScript_);
    if (!script || !script->ast || currentBlock_.empty()) {
      if (!callStack_.empty()) {
        const CallFrame frame = callStack_.back();
        callStack_.pop_back();
        currentScript_ = frame.scriptName;
        currentBlock_ = frame.returnBlock;
        currentCmdIndex_ = 0;
        continue;
      }
      waitReason_ = VmWaitReason::Ended;
      return waitReason_;
    }

    const LuaTable *block = findBlock(*script, currentBlock_);
    if (!block) {
      waitReason_ = VmWaitReason::Ended;
      return waitReason_;
    }

    if (currentCmdIndex_ < block->array.size()) {
      const LuaValue &cmdVal = block->array[currentCmdIndex_];
      ++currentCmdIndex_;
      if (cmdVal.isTable()) {
        bool jumped = false;
        bool yielded = false;
        executeCommand(*cmdVal.tableVal, *block, jumped, yielded);
        if (yielded)
          return waitReason_;
        if (jumped)
          continue;
      }
      continue;
    }

    // End of block: advance to linknext or next block in table order.
    const std::string nextBlock =
        resolveNextBlock(*script, currentBlock_, *block);
    if (!nextBlock.empty()) {
      currentBlock_ = nextBlock;
      currentCmdIndex_ = 0;
      continue;
    }

    if (!callStack_.empty()) {
      const CallFrame frame = callStack_.back();
      callStack_.pop_back();
      currentScript_ = frame.scriptName;
      currentBlock_ = frame.returnBlock;
      currentCmdIndex_ = 0;
      continue;
    }

    waitReason_ = VmWaitReason::Ended;
    return waitReason_;
  }

  return waitReason_;
}

bool ArtemisVm::advance() {
  if (waitReason_ == VmWaitReason::Dialogue) {
    dialogue_.hasText = false;
    waitReason_ = VmWaitReason::None;
    step();
    return true;
  }
  if (waitReason_ == VmWaitReason::WaitTimer) {
    waitTimerMs_ = 0;
    waitReason_ = VmWaitReason::None;
    step();
    return true;
  }
  if (waitReason_ == VmWaitReason::Movie && movie_.cancellable) {
    movie_.active = false;
    waitReason_ = VmWaitReason::None;
    step();
    return true;
  }
  return false;
}

bool ArtemisVm::selectChoice(std::size_t index) {
  if (waitReason_ != VmWaitReason::Choice || index >= choices_.size())
    return false;

  const ChoiceItem chosen = choices_[index];
  choices_.clear();
  waitReason_ = VmWaitReason::None;

  if (!chosen.exp.empty())
    evalExpression(chosen.exp);

  if (!chosen.targetFile.empty() || !chosen.targetLabel.empty() ||
      !chosen.targetBlock.empty()) {
    const std::string targetScript =
        chosen.targetFile.empty() ? currentScript_ : chosen.targetFile;
    const std::string targetBlk =
        !chosen.targetBlock.empty() ? chosen.targetBlock : chosen.targetLabel;
    if (const ParsedAstScript *script = getOrLoadScript(targetScript)) {
      currentScript_ = targetScript;
      currentBlock_ = resolveLabelToBlock(*script, targetBlk);
      currentCmdIndex_ = 0;
    }
  }

  step();
  return true;
}

void ArtemisVm::finishMovie() {
  if (waitReason_ == VmWaitReason::Movie) {
    movie_.active = false;
    waitReason_ = VmWaitReason::None;
    step();
  }
}

void ArtemisVm::finishWaitTimer() {
  if (waitReason_ == VmWaitReason::WaitTimer) {
    waitTimerMs_ = 0;
    waitReason_ = VmWaitReason::None;
    step();
  }
}

std::vector<SeEvent> ArtemisVm::consumeSeEvents() {
  std::vector<SeEvent> out;
  out.swap(pendingSe_);
  return out;
}

std::vector<VoiceEvent> ArtemisVm::consumeVoiceEvents() {
  std::vector<VoiceEvent> out;
  out.swap(pendingVoice_);
  return out;
}

std::vector<QuakeEvent> ArtemisVm::consumeQuakeEvents() {
  std::vector<QuakeEvent> out;
  out.swap(pendingQuake_);
  return out;
}

double ArtemisVm::getVar(const std::string &name) const {
  auto it = vars_.find(name);
  return (it != vars_.end()) ? it->second : 0.0;
}

void ArtemisVm::setVar(const std::string &name, double value) {
  vars_[name] = value;
}

bool ArtemisVm::evalCondition(std::string_view expr) const {
  const std::string s = trim(expr);
  if (s.empty() || s == "true")
    return true;
  if (s == "false")
    return false;

  // Handle simple negation `!x` or `not x`.
  if (s[0] == '!')
    return !evalCondition(s.substr(1));
  if (s.rfind("not ", 0) == 0)
    return !evalCondition(s.substr(4));

  // Handle `and` / `&&` and `or` / `||`.
  if (auto pos = s.find("&&"); pos != std::string::npos)
    return evalCondition(s.substr(0, pos)) && evalCondition(s.substr(pos + 2));
  if (auto pos = s.find("||"); pos != std::string::npos)
    return evalCondition(s.substr(0, pos)) || evalCondition(s.substr(pos + 2));

  auto evalAtom = [&](const std::string &tok) -> double {
    const std::string t = trim(tok);
    if (t.empty() || t == "false" || t == "nil")
      return 0.0;
    if (t == "true")
      return 1.0;
    char *end = nullptr;
    const double n = std::strtod(t.c_str(), &end);
    if (end && end != t.c_str() && *end == '\0')
      return n;
    return getVar(t);
  };

  for (const char *op : {"==", "~=", "!=", ">=", "<=", ">", "<"}) {
    const auto pos = s.find(op);
    if (pos == std::string::npos)
      continue;
    const double lhs = evalAtom(s.substr(0, pos));
    const double rhs = evalAtom(s.substr(pos + std::strlen(op)));
    const std::string_view opv(op);
    if (opv == "==")
      return lhs == rhs;
    if (opv == "~=" || opv == "!=")
      return lhs != rhs;
    if (opv == ">=")
      return lhs >= rhs;
    if (opv == "<=")
      return lhs <= rhs;
    if (opv == ">")
      return lhs > rhs;
    if (opv == "<")
      return lhs < rhs;
  }

  return evalAtom(s) != 0.0;
}

void ArtemisVm::evalExpression(std::string_view expr) {
  // Split multiple semicolon-separated statements.
  std::size_t start = 0;
  while (start < expr.size()) {
    const auto semi = expr.find(';', start);
    const std::string stmt =
        trim(semi == std::string_view::npos ? expr.substr(start)
                                            : expr.substr(start, semi - start));
    start = (semi == std::string_view::npos) ? expr.size() : semi + 1;
    if (stmt.empty())
      continue;

    const auto eq = stmt.find('=');
    if (eq == std::string::npos)
      continue;
    if (eq + 1 < stmt.size() && stmt[eq + 1] == '=')
      continue; // comparison, not assignment

    const std::string lhs = trim(stmt.substr(0, eq));
    const std::string rhs = trim(stmt.substr(eq + 1));
    if (lhs.empty() || rhs.empty())
      continue;

    auto evalAtom = [&](const std::string &tok) -> double {
      const std::string t = trim(tok);
      if (t == "true")
        return 1.0;
      if (t == "false" || t == "nil")
        return 0.0;
      char *end = nullptr;
      const double n = std::strtod(t.c_str(), &end);
      if (end && end != t.c_str() && *end == '\0')
        return n;
      return getVar(t);
    };

    double val = 0.0;
    if (auto plus = rhs.find('+'); plus != std::string::npos) {
      val = evalAtom(rhs.substr(0, plus)) + evalAtom(rhs.substr(plus + 1));
    } else if (auto minus = rhs.find('-', 1); minus != std::string::npos) {
      val = evalAtom(rhs.substr(0, minus)) - evalAtom(rhs.substr(minus + 1));
    } else {
      val = evalAtom(rhs);
    }
    setVar(lhs, val);
  }
}

std::vector<std::string> ArtemisVm::currentScriptBlocks() const {
  auto it = scriptCache_.find(currentScript_);
  if (it == scriptCache_.end())
    return {};
  return it->second.blockOrder;
}

bool ArtemisVm::jumpToBlock(const std::string &blockName) {
  const ParsedAstScript *script = getOrLoadScript(currentScript_);
  if (!script || !script->ast || !script->ast->getTable(blockName))
    return false;
  currentBlock_ = blockName;
  currentCmdIndex_ = 0;
  waitReason_ = VmWaitReason::None;
  choices_.clear();
  dialogue_.hasText = false;
  step();
  return true;
}

std::string ArtemisVm::serializeState() const {
  std::ostringstream oss;
  oss << "ARTEMIS_SAVE_V1\n";
  oss << "LOC|" << escapeField(currentScript_) << "|"
      << escapeField(currentBlock_) << "|" << currentCmdIndex_ << "|"
      << escapeField(preferredLanguage_) << "|" << escapeField(saveTitle_)
      << "\n";
  oss << "BG|" << escapeField(bg_.file) << "|" << escapeField(bg_.path) << "|"
      << bg_.x << "|" << bg_.y << "|" << bg_.ax << "|" << bg_.ay << "|"
      << bg_.bx << "|" << bg_.by << "|" << bg_.zoom << "|" << bg_.alpha << "|"
      << (bg_.isSolidColor ? 1 : 0) << "|" << static_cast<int>(bg_.solidR)
      << "|" << static_cast<int>(bg_.solidG) << "|"
      << static_cast<int>(bg_.solidB) << "\n";
  oss << "BGM|" << escapeField(bgm_.file) << "|" << bgm_.volume << "|"
      << (bgm_.playing ? 1 : 0) << "\n";
  oss << "TONE|" << escapeField(colorTone_) << "\n";
  for (const auto &s : sprites_) {
    oss << "SPR|" << s.id << "|" << s.lv << "|" << escapeField(s.ch) << "|"
        << escapeField(s.path) << "|" << escapeField(s.file) << "|"
        << escapeField(s.file1) << "|" << escapeField(s.file2) << "|" << s.x
        << "|" << s.y << "|" << s.alpha << "|" << s.zoom << "\n";
  }
  for (const auto &frame : callStack_) {
    oss << "CALL|" << escapeField(frame.scriptName) << "|"
        << escapeField(frame.returnBlock) << "\n";
  }
  for (const auto &[k, v] : vars_) {
    oss << "VAR|" << escapeField(k) << "|" << v << "\n";
  }
  return oss.str();
}

bool ArtemisVm::deserializeState(std::string_view data) {
  std::istringstream iss{std::string(data)};
  std::string header;
  if (!std::getline(iss, header))
    return false;
  if (!header.empty() && header.back() == '\r')
    header.pop_back();
  if (header != "ARTEMIS_SAVE_V1")
    return false;

  reset();

  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;
    const auto p = splitPipe(line);
    if (p.empty())
      continue;

    if (p[0] == "LOC" && p.size() >= 6) {
      currentScript_ = p[1];
      currentBlock_ = p[2];
      currentCmdIndex_ =
          static_cast<std::size_t>(std::strtoull(p[3].c_str(), nullptr, 10));
      preferredLanguage_ = p[4];
      saveTitle_ = p[5];
    } else if (p[0] == "BG" && p.size() >= 15) {
      bg_.file = p[1];
      bg_.path = p[2];
      bg_.x = std::stoi(p[3]);
      bg_.y = std::stoi(p[4]);
      bg_.ax = std::stoi(p[5]);
      bg_.ay = std::stoi(p[6]);
      bg_.bx = std::stoi(p[7]);
      bg_.by = std::stoi(p[8]);
      bg_.zoom = std::stoi(p[9]);
      bg_.alpha = std::stoi(p[10]);
      bg_.isSolidColor = std::stoi(p[11]) != 0;
      bg_.solidR = static_cast<std::uint8_t>(std::stoi(p[12]));
      bg_.solidG = static_cast<std::uint8_t>(std::stoi(p[13]));
      bg_.solidB = static_cast<std::uint8_t>(std::stoi(p[14]));
    } else if (p[0] == "BGM" && p.size() >= 4) {
      bgm_.file = p[1];
      bgm_.volume = std::stoi(p[2]);
      bgm_.playing = std::stoi(p[3]) != 0;
      ++bgm_.generation;
    } else if (p[0] == "TONE" && p.size() >= 2) {
      colorTone_ = p[1];
    } else if (p[0] == "SPR" && p.size() >= 12) {
      SpriteLayer s;
      s.id = std::stoi(p[1]);
      s.lv = std::stoi(p[2]);
      s.ch = p[3];
      s.path = p[4];
      s.file = p[5];
      s.file1 = p[6];
      s.file2 = p[7];
      s.x = std::stoi(p[8]);
      s.y = std::stoi(p[9]);
      s.alpha = std::stoi(p[10]);
      s.zoom = std::stoi(p[11]);
      s.visible = true;
      sprites_.push_back(std::move(s));
    } else if (p[0] == "CALL" && p.size() >= 3) {
      callStack_.push_back(CallFrame{p[1], p[2]});
    } else if (p[0] == "VAR" && p.size() >= 3) {
      vars_[p[1]] = std::strtod(p[2].c_str(), nullptr);
    }
  }

  // Re-run current block from start so dialogue/choices are restored cleanly.
  currentCmdIndex_ = 0;
  waitReason_ = VmWaitReason::None;
  step();
  return !currentScript_.empty();
}

} // namespace Artemis
