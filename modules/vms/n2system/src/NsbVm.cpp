// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/NsbVm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace N2System {

namespace {

std::string toLowerAscii(std::string_view sv) {
  std::string out(sv);
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + ('a' - 'A'));
  }
  return out;
}

std::string normalizeAssetPath(std::string_view raw) {
  std::string out(raw);
  std::replace(out.begin(), out.end(), '\\', '/');
  while (!out.empty() && out.front() == '/')
    out.erase(out.begin());
  return out;
}

std::string extractXmlAttr(std::string_view tag, std::string_view attrName) {
  const std::string needle = std::string(attrName) + "=";
  std::size_t pos = tag.find(needle);
  if (pos == std::string_view::npos)
    return {};
  pos += needle.size();
  if (pos >= tag.size())
    return {};
  const char quote = tag[pos];
  if (quote == '"' || quote == '\'') {
    ++pos;
    const auto end = tag.find(quote, pos);
    if (end != std::string_view::npos)
      return std::string(tag.substr(pos, end - pos));
  } else {
    auto end = tag.find_first_of(" \t>", pos);
    if (end == std::string_view::npos)
      end = tag.size();
    return std::string(tag.substr(pos, end - pos));
  }
  return {};
}

bool parseNumber(std::string_view s, std::int64_t &outInt, double &outFloat,
                 bool &isFloat) {
  if (s.empty())
    return false;
  std::string str(s);
  isFloat = (str.find('.') != std::string::npos);
  try {
    if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
      outInt = std::stoll(str, nullptr, 16);
      outFloat = static_cast<double>(outInt);
      isFloat = false;
      return true;
    }
    if (isFloat) {
      std::size_t idx = 0;
      outFloat = std::stod(str, &idx);
      outInt = static_cast<std::int64_t>(outFloat);
      return idx == str.size();
    }
    std::size_t idx = 0;
    outInt = std::stoll(str, &idx, 10);
    outFloat = static_cast<double>(outInt);
    return idx == str.size();
  } catch (...) {
    return false;
  }
}

float resolveCoord(const NsbValue &val, float current, float screenDim,
                   bool isX) {
  if (val.kind == NsbValue::Kind::String) {
    const auto lower = toLowerAscii(val.strVal);
    if (lower == "center" || lower == "incenter")
      return screenDim * 0.5f;
    if (lower == "left" || lower == "inleft" || lower == "top" ||
        lower == "intop")
      return 0.0f;
    if (lower == "right" || lower == "inright" || lower == "bottom" ||
        lower == "inbottom")
      return screenDim;
    if (lower == "outleft" || lower == "outtop")
      return -screenDim;
    if (lower == "outright" || lower == "outbottom")
      return screenDim;
    if (lower == "null" || lower.empty())
      return current;
  }
  (void)isX;
  const float num = static_cast<float>(val.asFloat());
  return val.isDelta ? (current + num) : num;
}

bool isHoverOnlyChoiceChild(std::string_view name) {
  const std::string lower = toLowerAscii(name);
  return lower.find("/mouseover/") != std::string::npos ||
         lower.find("/mouseclick/") != std::string::npos ||
         lower.find("/mouseleave/") != std::string::npos;
}

void applyAlignmentFlags(Entity &e, const NsbValue &xVal,
                         const NsbValue &yVal) {
  e.alignCenterX = e.alignRightX = false;
  e.alignCenterY = e.alignBottomY = false;
  if (xVal.kind == NsbValue::Kind::String) {
    const auto lx = toLowerAscii(xVal.strVal);
    if (lx == "center" || lx == "incenter")
      e.alignCenterX = true;
    else if (lx == "right" || lx == "inright")
      e.alignRightX = true;
  }
  if (yVal.kind == NsbValue::Kind::String) {
    const auto ly = toLowerAscii(yVal.strVal);
    if (ly == "center" || ly == "incenter")
      e.alignCenterY = true;
    else if (ly == "bottom" || ly == "inbottom")
      e.alignBottomY = true;
  }
}

} // namespace

// --- NsbValue ---

std::int64_t NsbValue::asInt() const {
  if (kind == Kind::Int)
    return intVal;
  if (kind == Kind::Float)
    return static_cast<std::int64_t>(floatVal);
  if (strVal == "true" || strVal == "TRUE")
    return 1;
  if (strVal == "false" || strVal == "FALSE" || strVal == "null" ||
      strVal == "NULL" || strVal.empty())
    return 0;
  std::int64_t i = 0;
  double f = 0.0;
  bool isF = false;
  if (parseNumber(strVal, i, f, isF))
    return i;
  return 0;
}

double NsbValue::asFloat() const {
  if (kind == Kind::Float)
    return floatVal;
  if (kind == Kind::Int)
    return static_cast<double>(intVal);
  std::int64_t i = 0;
  double f = 0.0;
  bool isF = false;
  if (parseNumber(strVal, i, f, isF))
    return f;
  return static_cast<double>(asInt());
}

std::string NsbValue::asString() const {
  if (kind == Kind::String)
    return strVal;
  if (kind == Kind::Int)
    return std::to_string(intVal);
  std::ostringstream oss;
  oss << floatVal;
  return oss.str();
}

bool NsbValue::isTruthy() const {
  if (kind == Kind::Int)
    return intVal != 0;
  if (kind == Kind::Float)
    return floatVal != 0.0;
  if (strVal.empty() || strVal == "false" || strVal == "FALSE" ||
      strVal == "0" || strVal == "null" || strVal == "NULL")
    return false;
  return true;
}

// --- PreDialogueBlock parser ---

PreDialogueBlock NsbVm::parsePreBlock(std::string_view id,
                                      std::string_view boxName,
                                      std::string_view rawXml) {
  PreDialogueBlock block;
  block.id = std::string(id);
  block.boxName = std::string(boxName);
  block.rawXml = std::string(rawXml);

  std::string out;
  out.reserve(rawXml.size());
  std::size_t i = 0;
  while (i < rawXml.size()) {
    if (rawXml[i] == '<') {
      const auto close = rawXml.find('>', i + 1);
      if (close == std::string_view::npos) {
        out.push_back(rawXml[i++]);
        continue;
      }
      const auto tag = rawXml.substr(i + 1, close - i - 1);
      const auto lower = toLowerAscii(tag);
      if (lower.compare(0, 5, "voice") == 0) {
        const auto name = extractXmlAttr(tag, "name");
        const auto src = extractXmlAttr(tag, "src");
        if (!name.empty())
          block.speaker = name;
        if (!src.empty()) {
          std::string vp = normalizeAssetPath(src);
          if (vp.size() < 4 || vp.compare(vp.size() - 4, 4, ".ogg") != 0)
            vp += ".ogg";
          block.voicePath = std::move(vp);
        }
      } else if (lower == "br" || lower == "br/" || lower == "br /") {
        out.push_back('\n');
      } else if (lower == "k" || lower == "k/") {
        if (!out.empty() && out.back() != '\n')
          out.push_back(' ');
      }
      i = close + 1;
      continue;
    }
    // Strip leading [textNNNNN] block header marker
    if (rawXml[i] == '[' && i + 5 < rawXml.size() &&
        rawXml.compare(i + 1, 4, "text") == 0) {
      const auto close = rawXml.find(']', i + 1);
      if (close != std::string_view::npos && close - i < 24) {
        i = close + 1;
        while (i < rawXml.size() && (rawXml[i] == '\r' || rawXml[i] == '\n'))
          ++i;
        continue;
      }
    }
    if (rawXml[i] == '\r') {
      ++i;
      continue;
    }
    out.push_back(rawXml[i++]);
  }

  // Trim leading/trailing newlines
  while (!out.empty() && (out.front() == '\n' || out.front() == ' '))
    out.erase(out.begin());
  while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
    out.pop_back();

  block.text = std::move(out);
  return block;
}

// --- NsbVm ---

NsbVm::NsbVm() { syncSystemVars(); }

void NsbVm::setConfig(const SystemIniConfig &cfg) {
  config_ = cfg;
  audioSystem_.setVolume(AudioChannelKind::Bgm,
                         std::clamp(cfg.bgmVolume / 1000.0f, 0.0f, 1.0f));
  audioSystem_.setVolume(AudioChannelKind::Se,
                         std::clamp(cfg.seVolume / 1000.0f, 0.0f, 1.0f));
  audioSystem_.setVolume(AudioChannelKind::Voice,
                         std::clamp(cfg.voiceVolume / 1000.0f, 0.0f, 1.0f));
}

void NsbVm::syncSystemVars() {
  auto initIfMissing = [&](std::string_view k, NsbValue v) {
    if (vars_.find(std::string(k)) == vars_.end())
      vars_.emplace(std::string(k), std::move(v));
  };
  initIfMissing("$SYSTEM_menu_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_skip", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_skip_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_text_auto", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_text_auto_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_backlog_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_save_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_load_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_reset_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_window_full", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_window_full_lock", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_present_process", NsbValue::makeString("main"));
  initIfMissing("$SYSTEM_present_text", NsbValue::makeString(""));
  initIfMissing("$SYSTEM_last_text", NsbValue::makeString(""));
  initIfMissing("$SYSTEM_mousewheel_up", NsbValue::makeString("false"));
  initIfMissing("$SYSTEM_mousewheel_down", NsbValue::makeString("false"));
  initIfMissing("#SYSTEM_skip_express", NsbValue::makeString("false"));
  initIfMissing("#SYSTEM_skip_absolute", NsbValue::makeString("false"));
  initIfMissing("#SYSTEM_text_speed", NsbValue::makeInt(50));
  initIfMissing("#SYSTEM_text_auto_speed", NsbValue::makeInt(50));
  initIfMissing("#SYSTEM_sound_volume_bgm",
                NsbValue::makeInt(config_.bgmVolume));
  initIfMissing("#SYSTEM_sound_volume_se", NsbValue::makeInt(config_.seVolume));
  initIfMissing("#SYSTEM_sound_volume_voice",
                NsbValue::makeInt(config_.voiceVolume));
  initIfMissing("#SYSTEM_product_code", NsbValue::makeString("STEINS;GATE"));
}

std::shared_ptr<NsbScript> NsbVm::loadScript(std::string_view path) {
  const std::string nsbPath = NsbScript::nssToNsbPath(path);
  if (auto it = scripts_.find(nsbPath); it != scripts_.end())
    return it->second;
  if (!loader_)
    return nullptr;

  std::vector<std::uint8_t> nsbBytes;
  std::vector<std::uint8_t> mapBytes;
  if (!loader_(nsbPath, nsbBytes, mapBytes))
    return nullptr;

  auto script = std::make_shared<NsbScript>(
      NsbScript::parse(nsbPath, nsbBytes, mapBytes));
  scripts_[nsbPath] = script;

  // Recursively preload included scripts so CallFunction resolves cleanly.
  for (const auto &inc : script->includes()) {
    const std::string incNsb = NsbScript::nssToNsbPath(inc);
    if (scripts_.find(incNsb) == scripts_.end()) {
      (void)loadScript(inc);
    }
  }
  return script;
}

bool NsbVm::boot(std::string_view startScript,
                 std::string_view initialChapter) {
  threads_.clear();
  pendingSaveAction_.reset();
  activeChoices_.clear();
  triggeredChoice_.clear();
  activeMovie_ = {};
  sceneGraph_.clear();
  soundEntities_.clear();
  soundTypes_.clear();
  soundVolumes_.clear();
  soundLoops_.clear();
  dialogueSystem_.clearDialogue();
  finished_ = false;
  totalExecuted_ = 0;
  syncSystemVars();

  auto script = loadScript(startScript);
  if (!script)
    return false;

  std::size_t startPc = script->findChapter(initialChapter);
  if (startPc == std::string::npos)
    startPc = 0;

  Thread mainThread;
  mainThread.name = "main";
  mainThread.script = script;
  mainThread.pc = startPc;
  mainThread.running = true;
  threads_.push_back(std::move(mainThread));
  return true;
}

bool NsbVm::jumpToScript(std::string_view scriptPath,
                         std::string_view chapterOrScene) {
  auto script = loadScript(scriptPath);
  if (!script)
    return false;
  std::size_t pc = script->findSymbol(chapterOrScene);
  if (pc == std::string::npos)
    pc = script->findChapter(chapterOrScene);
  if (pc == std::string::npos)
    pc = script->findScene(chapterOrScene);
  if (pc == std::string::npos)
    pc = 0;

  threads_.clear();
  pendingSaveAction_.reset();
  activeChoices_.clear();
  triggeredChoice_.clear();
  activeMovie_ = {};
  finished_ = false;

  Thread mainThread;
  mainThread.name = "main";
  mainThread.script = script;
  mainThread.pc = pc;
  mainThread.running = true;
  threads_.push_back(std::move(mainThread));
  return true;
}

bool NsbVm::resolveFunction(const std::shared_ptr<NsbScript> &fromScript,
                            std::string_view funcName,
                            std::shared_ptr<NsbScript> &outScript,
                            std::size_t &outPc) {
  if (fromScript) {
    if (auto pc = fromScript->findFunction(funcName); pc != std::string::npos) {
      outScript = fromScript;
      outPc = pc;
      return true;
    }
    for (const auto &inc : fromScript->includes()) {
      auto incScript = loadScript(inc);
      if (incScript) {
        if (auto pc = incScript->findFunction(funcName);
            pc != std::string::npos) {
          outScript = incScript;
          outPc = pc;
          return true;
        }
      }
    }
  }
  for (const auto &[path, script] : scripts_) {
    (void)path;
    if (auto pc = script->findFunction(funcName); pc != std::string::npos) {
      outScript = script;
      outPc = pc;
      return true;
    }
  }
  return false;
}

bool NsbVm::resolveTarget(const std::shared_ptr<NsbScript> &fromScript,
                          std::string_view rawTarget, bool isScene,
                          std::shared_ptr<NsbScript> &outScript,
                          std::size_t &outPc) {
  std::string target(rawTarget);
  if (!target.empty() && (target[0] == '$' || target[0] == '#')) {
    target = readVar(target).asString();
  }
  if (target.empty())
    return false;

  std::string filePart;
  std::string symPart;
  const auto arrow = target.find("->");
  if (arrow != std::string::npos) {
    filePart = target.substr(0, arrow);
    symPart = target.substr(arrow + 2);
  } else if (target.find(".nss") != std::string::npos ||
             target.find(".nsb") != std::string::npos ||
             target.find('/') != std::string::npos) {
    filePart = target;
    symPart = "main";
  } else {
    filePart = "@";
    symPart = target;
  }

  std::shared_ptr<NsbScript> targetScript = fromScript;
  if (!filePart.empty() && filePart != "@") {
    targetScript = loadScript(filePart);
    if (!targetScript && filePart.find("nss/") != 0)
      targetScript = loadScript("nss/" + filePart);
  }
  if (!targetScript)
    return false;

  std::size_t pc = std::string::npos;
  if (isScene) {
    pc = targetScript->findScene(symPart);
    if (pc == std::string::npos)
      pc = targetScript->findChapter(symPart);
    if (pc == std::string::npos)
      pc = targetScript->findSymbol(symPart);
  } else {
    pc = targetScript->findChapter(symPart);
    if (pc == std::string::npos)
      pc = targetScript->findScene(symPart);
    if (pc == std::string::npos)
      pc = targetScript->findSymbol(symPart);
  }

  // Fallback: if scene name starts with "scene." or script filename prefix
  if (pc == std::string::npos) {
    for (const auto &[k, idx] : targetScript->symbols()) {
      if (isScene && k.compare(0, 6, "scene.") == 0 &&
          (symPart == "main" || k.find(symPart) != std::string::npos)) {
        pc = idx;
        break;
      }
      if (!isScene && k.compare(0, 8, "chapter.") == 0 &&
          (symPart == "main" || k.find(symPart) != std::string::npos)) {
        pc = idx;
        break;
      }
    }
  }
  if (pc == std::string::npos)
    pc = 0;

  outScript = targetScript;
  outPc = pc;
  return true;
}

std::vector<NsbValue> NsbVm::popArgs(Thread &t, std::size_t count) {
  std::vector<NsbValue> out(count);
  for (std::size_t i = count; i > 0; --i) {
    if (!t.stack.empty()) {
      out[i - 1] = std::move(t.stack.back());
      t.stack.pop_back();
    }
  }
  return out;
}

NsbValue NsbVm::popOne(Thread &t) {
  if (t.stack.empty())
    return NsbValue::makeInt(0);
  NsbValue v = std::move(t.stack.back());
  t.stack.pop_back();
  return v;
}

void NsbVm::assignVar(Thread &t, std::string_view varName,
                      const NsbValue &val) {
  std::string key(varName);
  if (!t.stack.empty() && !t.stack.back().lvalueKey.empty()) {
    const auto &lv = t.stack.back().lvalueKey;
    if (lv.compare(0, key.size(), key) == 0 &&
        (lv.size() == key.size() || lv[key.size()] == '[')) {
      key = lv;
      t.stack.pop_back();
    }
  }
  NsbValue stored = val;
  stored.lvalueKey.clear();
  vars_[key] = stored;
  if (stored.kind == NsbValue::Kind::String)
    legacyVars_.setString(key, stored.strVal);
  else
    legacyVars_.setInt(key, stored.asInt());
}

NsbValue NsbVm::readVar(std::string_view varName) const {
  auto it = vars_.find(std::string(varName));
  if (it != vars_.end())
    return it->second;
  return NsbValue::makeInt(0);
}

std::vector<std::string>
NsbVm::matchingSoundEntities(std::string_view query) const {
  std::vector<std::string> out;
  if (query.empty())
    return out;
  std::string name(query);
  // "@BGM04" is an entity path, "BGM04" is the creation name. Function
  // arguments in retail scripts use both spellings for the same sound.
  if (name.front() == '@')
    name.erase(name.begin());
  if (const auto slash = name.find('/'); slash != std::string::npos)
    name.erase(slash);
  if (name.empty())
    return out;
  if (soundEntities_.find(name) != soundEntities_.end()) {
    out.push_back(name);
    return out;
  }
  for (const auto &[entity, path] : soundEntities_) {
    (void)path;
    if (SceneGraph::wildcardMatch(entity, name))
      out.push_back(entity);
  }
  return out;
}

NsbValue NsbVm::resolveVariableName(const NsbValue &value) const {
  if (value.kind != NsbValue::Kind::String || value.strVal.empty())
    return value;
  if (auto it = vars_.find(value.strVal); it != vars_.end())
    return it->second;
  if (value.strVal.front() != '$' && value.strVal.front() != '#') {
    if (auto it = vars_.find("$" + value.strVal); it != vars_.end())
      return it->second;
  }
  return value;
}

const NsbValue *NsbVm::findVar(std::string_view name) const {
  auto it = vars_.find(std::string(name));
  return it == vars_.end() ? nullptr : &it->second;
}

void NsbVm::setVar(std::string_view name, const NsbValue &val) {
  vars_[std::string(name)] = val;
}

std::uint32_t NsbVm::parseColorName(std::string_view colorStr) {
  const auto lower = toLowerAscii(colorStr);
  if (lower == "black")
    return 0xFF000000u;
  if (lower == "white")
    return 0xFFFFFFFFu;
  if (lower == "red")
    return 0xFFFF0000u;
  if (lower == "green")
    return 0xFF00FF00u;
  if (lower == "blue")
    return 0xFF0000FFu;
  if (lower == "gray" || lower == "grey")
    return 0xFF808080u;
  if (lower.size() >= 6) {
    std::string hex = lower;
    if (hex[0] == '#')
      hex.erase(hex.begin());
    else if (hex.size() > 2 && hex[0] == '0' && hex[1] == 'x')
      hex.erase(0, 2);
    try {
      const std::uint32_t v =
          static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16));
      return hex.size() <= 6 ? (0xFF000000u | v) : v;
    } catch (...) {
    }
  }
  return 0xFF000000u;
}

void NsbVm::update(float dtSeconds) {
  elapsedSeconds_ += dtSeconds;
  sceneGraph_.update(dtSeconds);
  audioSystem_.update(dtSeconds);
  dialogueSystem_.update(dtSeconds);

  for (auto &t : threads_) {
    if (!t.running)
      continue;
    if (t.waitTimer > 0.0f) {
      t.waitTimer = std::max(0.0f, t.waitTimer - dtSeconds);
    }
    if (t.waitingKey && t.waitKeyTimeout > 0.0f) {
      t.waitKeyTimeout -= dtSeconds;
      if (t.waitKeyTimeout <= 0.0f) {
        t.waitingKey = false;
        t.waitKeyTimeout = -1.0f;
      }
    }
    if (t.waitEntityAnim) {
      bool anyBusy = false;
      for (auto *e : sceneGraph_.queryEntities(t.waitEntity)) {
        if (e->isAnimating()) {
          anyBusy = true;
          break;
        }
      }
      if (!anyBusy) {
        t.waitEntityAnim = false;
        t.waitEntity.clear();
      }
    }
  }
}

bool NsbVm::waitingForKey() const {
  if (threads_.empty())
    return false;
  const auto &main = threads_.front();
  return main.waitingKey || main.waitingText || dialogueSystem_.isTyping();
}

bool NsbVm::advance() {
  if (dialogueSystem_.isTyping()) {
    dialogueSystem_.finishTyping();
    return true;
  }
  bool consumed = false;
  if (!threads_.empty()) {
    auto &main = threads_.front();
    if (main.waitingKey) {
      main.waitingKey = false;
      main.waitKeyTimeout = -1.0f;
      consumed = true;
    }
    if (main.waitingText) {
      main.waitingText = false;
      dialogueSystem_.advance();
      consumed = true;
    }
    if (main.waitTimer > 0.0f) {
      main.waitTimer = 0.0f;
      consumed = true;
    }
    if (main.waitEntityAnim) {
      for (auto *e : sceneGraph_.queryEntities(main.waitEntity)) {
        e->update(1000.0f);
      }
      main.waitEntityAnim = false;
      main.waitEntity.clear();
      consumed = true;
    }
    if (main.waitingMovie && activeMovie_.active && !activeMovie_.loop) {
      notifyMovieFinished(activeMovie_.entityName);
      consumed = true;
    }
  }
  return consumed;
}

void NsbVm::triggerChoice(std::string_view choiceEntityName) {
  triggeredChoice_ = std::string(choiceEntityName);
  for (auto &t : threads_) {
    t.waitingChoice = false;
  }
}

void NsbVm::triggerChoiceIndex(int index) {
  if (index >= 0 && static_cast<std::size_t>(index) < activeChoices_.size()) {
    triggerChoice(activeChoices_[static_cast<std::size_t>(index)]);
  }
}

void NsbVm::notifyMovieFinished(std::string_view entityName) {
  if (entityName.empty() || activeMovie_.entityName == entityName) {
    if (auto *e = sceneGraph_.findEntity(activeMovie_.entityName)) {
      e->moviePlaying = false;
    }
    activeMovie_.active = false;
    for (auto &t : threads_)
      t.waitingMovie = false;
  }
}

std::string NsbVm::currentScriptPath() const {
  if (threads_.empty() || !threads_.front().script)
    return {};
  return threads_.front().script->path();
}

std::uint32_t NsbVm::currentLine() const {
  if (threads_.empty() || !threads_.front().script)
    return 0;
  const auto &insns = threads_.front().script->instructions();
  if (threads_.front().pc < insns.size())
    return insns[threads_.front().pc].line;
  return 0;
}

std::size_t NsbVm::step(std::size_t budget) {
  if (finished_ || threads_.empty() || budget == 0 || pendingSaveAction_)
    return 0;

  // Snapshot the thread set at frame start. Creating a process during a VM
  // instruction must neither invalidate its current Thread reference nor run
  // its first instruction before the next scheduler tick.
  const std::size_t runnableAtStart = threads_.size();
  std::size_t total = 0;
  // Preserve the full native instruction budget for single-threaded titles;
  // reserve a fair slice for workers only once background processes exist.
  const std::size_t mainBudget =
      runnableAtStart == 1 ? budget : std::min<std::size_t>(budget, 512);
  if (threads_.front().running && !threads_.front().deleted)
    total += stepThread(threads_.front(), mainBudget);
  for (std::size_t i = 1;
       i < runnableAtStart && total < budget && !pendingSaveAction_; ++i) {
    if (threads_[i].running && !threads_[i].deleted)
      total +=
          stepThread(threads_[i], std::min<std::size_t>(256, budget - total));
  }

  // Do not erase while execInstruction() retains a reference to a Thread.
  // A stopped process stays allocated because Request(Start) can resume it.
  for (std::size_t i = 1; i < threads_.size();) {
    if (threads_[i].deleted)
      threads_.erase(threads_.begin() + static_cast<std::ptrdiff_t>(i));
    else
      ++i;
  }
  if (!threads_.front().running || threads_.front().deleted)
    finished_ = true;
  return total;
}

std::size_t NsbVm::stepThread(Thread &t, std::size_t budget) {
  std::size_t executed = 0;
  while (executed < budget && t.running && !t.deleted && t.script) {
    if (t.waitTimer > 0.0f || t.waitingKey || t.waitingText ||
        t.waitingChoice || t.waitEntityAnim || t.waitingMovie) {
      break;
    }
    const auto &insns = t.script->instructions();
    if (t.pc >= insns.size()) {
      if (!t.callStack.empty()) {
        auto frame = std::move(t.callStack.back());
        t.callStack.pop_back();
        // N2System function parameters are global variable slots: the script
        // intentionally reads them after a function returns (e.g. $MOVIESG is
        // set by CreateMovieSG and consumed later by MovieWaitSG). Do not
        // restore them here; EndFunctionStrategy used to do so.
        t.script = std::move(frame.script);
        t.pc = frame.pc;
        continue;
      }
      t.running = false;
      break;
    }

    const NsbInstruction &ins = insns[t.pc];
    ++t.pc;
    ++executed;
    ++totalExecuted_;

    bool yielded = false;
    execInstruction(t, ins, yielded);
    if (yielded)
      break;
  }
  return executed;
}

void NsbVm::execInstruction(Thread &t, const NsbInstruction &ins,
                            bool &yielded) {
  const auto op = ins.opcode;
  const auto &args = ins.args;
  const float screenW = static_cast<float>(config_.width);
  const float screenH = static_cast<float>(config_.height);

  switch (op) {
  // --- Stack & Literals ---
  case 0x0d0: { // LiteralStrategy: [type, value]
    if (args.size() >= 2) {
      if (args[0] == "INT") {
        std::int64_t iv = 0;
        double fv = 0.0;
        bool isF = false;
        parseNumber(args[1], iv, fv, isF);
        t.stack.push_back(isF ? NsbValue::makeFloat(fv)
                              : NsbValue::makeInt(iv));
      } else if (args[0] == "FLOAT") {
        std::int64_t iv = 0;
        double fv = 0.0;
        bool isF = false;
        parseNumber(args[1], iv, fv, isF);
        t.stack.push_back(NsbValue::makeFloat(fv));
      } else {
        t.stack.push_back(NsbValue::makeString(args[1]));
      }
    } else if (args.size() == 1) {
      t.stack.push_back(NsbValue::makeString(args[0]));
    }
    break;
  }
  case 0x0d1: { // VariableStrategy: [varName]
    if (!args.empty()) {
      NsbValue v = readVar(args[0]);
      v.lvalueKey = args[0];
      t.stack.push_back(std::move(v));
    }
    break;
  }
  case 0x091: { // AtExpressionStrategy: unary @ (relative delta)
    if (!t.stack.empty())
      t.stack.back().isDelta = true;
    break;
  }
  case 0x08e: { // EndOfLineStrategy: clear expression stack
    t.stack.clear();
    break;
  }
  case 0x0c8: // Block begin
  case 0x0c9: // Block end
  case 0x0da: // CallBegin
  case 0x0db: // CallEnd
  case 0x0dc: // PreCallFunction
  case 0x0d9: // PreEndFunction
    break;

  // --- Unary & Binary Expressions ---
  case 0x0d2: { // NegaExpressionStrategy
    auto v = popOne(t);
    if (v.kind == NsbValue::Kind::Float)
      t.stack.push_back(NsbValue::makeFloat(-v.floatVal));
    else
      t.stack.push_back(NsbValue::makeInt(-v.asInt()));
    break;
  }
  case 0x0d3: { // PosiExpressionStrategy
    break;
  }
  case 0x0ac: { // NotExpressionStrategy (!)
    auto v = popOne(t);
    t.stack.push_back(NsbValue::makeInt(v.isTruthy() ? 0 : 1));
    break;
  }
  case 0x0a5: { // AddExpressionStrategy (+)
    auto r = popOne(t);
    auto l = popOne(t);
    if (l.kind == NsbValue::Kind::String || r.kind == NsbValue::Kind::String) {
      t.stack.push_back(NsbValue::makeString(l.asString() + r.asString()));
    } else if (l.kind == NsbValue::Kind::Float ||
               r.kind == NsbValue::Kind::Float) {
      t.stack.push_back(NsbValue::makeFloat(l.asFloat() + r.asFloat()));
    } else {
      t.stack.push_back(NsbValue::makeInt(l.asInt() + r.asInt()));
    }
    break;
  }
  case 0x0a6: { // SubExpressionStrategy (-)
    auto r = popOne(t);
    auto l = popOne(t);
    if (l.kind == NsbValue::Kind::Float || r.kind == NsbValue::Kind::Float)
      t.stack.push_back(NsbValue::makeFloat(l.asFloat() - r.asFloat()));
    else
      t.stack.push_back(NsbValue::makeInt(l.asInt() - r.asInt()));
    break;
  }
  case 0x0a7: { // MulExpressionStrategy (*)
    auto r = popOne(t);
    auto l = popOne(t);
    if (l.kind == NsbValue::Kind::Float || r.kind == NsbValue::Kind::Float)
      t.stack.push_back(NsbValue::makeFloat(l.asFloat() * r.asFloat()));
    else
      t.stack.push_back(NsbValue::makeInt(l.asInt() * r.asInt()));
    break;
  }
  case 0x0a8: { // DivExpressionStrategy (/)
    auto r = popOne(t);
    auto l = popOne(t);
    const auto rv = r.asInt();
    t.stack.push_back(NsbValue::makeInt(rv == 0 ? 0 : l.asInt() / rv));
    break;
  }
  case 0x0a9: { // ModExpressionStrategy (%)
    auto r = popOne(t);
    auto l = popOne(t);
    const auto rv = r.asInt();
    t.stack.push_back(NsbValue::makeInt(rv == 0 ? 0 : l.asInt() % rv));
    break;
  }
  case 0x0aa: { // AndExpressionStrategy (&)
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asInt() & r.asInt()));
    break;
  }
  case 0x0ab: { // OrExpressionStrategy (|)
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asInt() | r.asInt()));
    break;
  }
  case 0x0ad: { // XorExpressionStrategy (^)
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asInt() ^ r.asInt()));
    break;
  }
  case 0x0ae: { // LShiftExpressionStrategy (<<)
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asInt() << (r.asInt() & 63)));
    break;
  }
  case 0x0af: { // RShiftExpressionStrategy (>>)
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asInt() >> (r.asInt() & 63)));
    break;
  }

  // --- Comparisons & Logical ---
  case 0x0c0: { // CmpEqualStrategy (==)
    auto r = popOne(t);
    auto l = popOne(t);
    bool eq = false;
    if (l.kind == NsbValue::Kind::String && r.kind == NsbValue::Kind::String)
      eq = (l.strVal == r.strVal);
    else if (l.kind == NsbValue::Kind::String ||
             r.kind == NsbValue::Kind::String)
      eq = (l.asString() == r.asString()) ||
           (l.isTruthy() == r.isTruthy() &&
            (l.asString() == "true" || l.asString() == "false" ||
             r.asString() == "true" || r.asString() == "false"));
    else
      eq = (l.asInt() == r.asInt());
    t.stack.push_back(NsbValue::makeInt(eq ? 1 : 0));
    break;
  }
  case 0x0c1: { // CmpNEStrategy (!=)
    auto r = popOne(t);
    auto l = popOne(t);
    bool eq = false;
    if (l.kind == NsbValue::Kind::String && r.kind == NsbValue::Kind::String)
      eq = (l.strVal == r.strVal);
    else if (l.kind == NsbValue::Kind::String ||
             r.kind == NsbValue::Kind::String)
      eq = (l.asString() == r.asString());
    else
      eq = (l.asInt() == r.asInt());
    t.stack.push_back(NsbValue::makeInt(eq ? 0 : 1));
    break;
  }
  case 0x0c2: { // <=
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asFloat() <= r.asFloat() ? 1 : 0));
    break;
  }
  case 0x0c3: { // >=
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asFloat() >= r.asFloat() ? 1 : 0));
    break;
  }
  case 0x0c4: { // <
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asFloat() < r.asFloat() ? 1 : 0));
    break;
  }
  case 0x0c5: { // >
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(NsbValue::makeInt(l.asFloat() > r.asFloat() ? 1 : 0));
    break;
  }
  case 0x0c6: { // &&
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(
        NsbValue::makeInt((l.isTruthy() && r.isTruthy()) ? 1 : 0));
    break;
  }
  case 0x0c7: { // ||
    auto r = popOne(t);
    auto l = popOne(t);
    t.stack.push_back(
        NsbValue::makeInt((l.isTruthy() || r.isTruthy()) ? 1 : 0));
    break;
  }

  // --- Assignments & Inc/Dec ---
  case 0x0b0: { // Assign (=)
    auto rhs = popOne(t);
    if (!args.empty())
      assignVar(t, args[0], rhs);
    t.stack.push_back(rhs);
    break;
  }
  case 0x0b1: { // AddAssign (+=)
    auto rhs = popOne(t);
    if (!args.empty()) {
      auto cur = readVar(args[0]);
      NsbValue res = (cur.kind == NsbValue::Kind::String ||
                      rhs.kind == NsbValue::Kind::String)
                         ? NsbValue::makeString(cur.asString() + rhs.asString())
                         : NsbValue::makeInt(cur.asInt() + rhs.asInt());
      assignVar(t, args[0], res);
      t.stack.push_back(res);
    }
    break;
  }
  case 0x0b2: { // SubAssign (-=)
    auto rhs = popOne(t);
    if (!args.empty()) {
      auto res = NsbValue::makeInt(readVar(args[0]).asInt() - rhs.asInt());
      assignVar(t, args[0], res);
      t.stack.push_back(res);
    }
    break;
  }
  case 0x0b3: { // MulAssign (*=)
    auto rhs = popOne(t);
    if (!args.empty()) {
      auto res = NsbValue::makeInt(readVar(args[0]).asInt() * rhs.asInt());
      assignVar(t, args[0], res);
      t.stack.push_back(res);
    }
    break;
  }
  case 0x0b4: { // DivAssign (/=)
    auto rhs = popOne(t);
    if (!args.empty()) {
      const auto rv = rhs.asInt();
      auto res = NsbValue::makeInt(rv == 0 ? 0 : readVar(args[0]).asInt() / rv);
      assignVar(t, args[0], res);
      t.stack.push_back(res);
    }
    break;
  }
  case 0x0b5: { // ModAssign (%=)
    auto rhs = popOne(t);
    if (!args.empty()) {
      const auto rv = rhs.asInt();
      auto res = NsbValue::makeInt(rv == 0 ? 0 : readVar(args[0]).asInt() % rv);
      assignVar(t, args[0], res);
      t.stack.push_back(res);
    }
    break;
  }
  case 0x0b6: { // StringAssign (.=)
    auto rhs = popOne(t);
    if (!args.empty()) {
      auto res =
          NsbValue::makeString(readVar(args[0]).asString() + rhs.asString());
      assignVar(t, args[0], res);
      t.stack.push_back(res);
    }
    break;
  }
  case 0x0b7:
  case 0x0b8:
  case 0x0b9:
  case 0x0ba:
  case 0x0bb: { // Bitwise compound assigns
    auto rhs = popOne(t);
    if (!args.empty()) {
      const auto lv = readVar(args[0]).asInt();
      const auto rv = rhs.asInt();
      std::int64_t r = lv;
      if (op == 0x0b7)
        r = lv & rv;
      else if (op == 0x0b8)
        r = lv | rv;
      else if (op == 0x0b9)
        r = lv ^ rv;
      else if (op == 0x0ba)
        r = lv << (rv & 63);
      else if (op == 0x0bb)
        r = lv >> (rv & 63);
      auto res = NsbValue::makeInt(r);
      assignVar(t, args[0], res);
      t.stack.push_back(res);
    }
    break;
  }
  case 0x0bc:   // PreInc (++x)
  case 0x0bd:   // PreDec (--x)
  case 0x0be:   // PostInc (x++)
  case 0x0bf: { // PostDec (x--)
    std::string key;
    if (!t.stack.empty()) {
      auto top = popOne(t);
      key = !top.lvalueKey.empty() ? top.lvalueKey : top.asString();
    } else if (!args.empty()) {
      key = args[0];
    }
    if (!key.empty()) {
      const auto oldVal = readVar(key).asInt();
      const auto delta = (op == 0x0bc || op == 0x0be) ? 1 : -1;
      const auto newVal = oldVal + delta;
      vars_[key] = NsbValue::makeInt(newVal);
      t.stack.push_back(
          NsbValue::makeInt((op == 0x0bc || op == 0x0bd) ? newVal : oldVal));
    }
    break;
  }

  // --- Arrays & Subscripts ---
  case 0x072: { // ArrayStrategy [varName, elem0, elem1, ...]
    if (!args.empty()) {
      const std::size_t elemCount = args.size() > 1 ? args.size() - 1 : 0;
      auto elems = popArgs(t, elemCount);
      std::string base = args[0];
      if (!t.stack.empty() && !t.stack.back().lvalueKey.empty()) {
        const auto &lv = t.stack.back().lvalueKey;
        if (lv.compare(0, base.size(), base) == 0 &&
            (lv.size() == base.size() || lv[base.size()] == '[')) {
          base = lv;
          t.stack.pop_back();
        }
      }
      auto &keys = arrayKeys_[base];
      keys.clear();
      for (std::size_t i = 0; i < elems.size(); ++i) {
        const std::string idxKey = std::to_string(i);
        keys.push_back(idxKey);
        vars_[base + "[" + idxKey + "]"] = elems[i];
      }
    }
    break;
  }
  case 0x073: { // AssocArrayStrategy [varName, key0, key1, ...]
    if (!args.empty()) {
      const std::size_t keyCount = args.size() > 1 ? args.size() - 1 : 0;
      auto keyVals = popArgs(t, keyCount);
      std::string base = args[0];
      if (!t.stack.empty() && !t.stack.back().lvalueKey.empty()) {
        const auto &lv = t.stack.back().lvalueKey;
        if (lv.compare(0, base.size(), base) == 0 &&
            (lv.size() == base.size() || lv[base.size()] == '[')) {
          base = lv;
          t.stack.pop_back();
        }
      }
      auto &keys = arrayKeys_[base];
      keys.clear();
      for (const auto &kv : keyVals) {
        keys.push_back(kv.asString());
      }
    }
    break;
  }
  case 0x0de: { // SubScriptStrategy [varName, dims]
    if (args.size() >= 2) {
      std::size_t dims = 1;
      try {
        dims = static_cast<std::size_t>(std::stoul(args[1]));
      } catch (...) {
      }
      auto indices = popArgs(t, dims);
      std::string fullKey = args[0];
      for (const auto &idx : indices) {
        fullKey += "[";
        fullKey += idx.asString();
        fullKey += "]";
      }
      NsbValue v = readVar(fullKey);
      v.lvalueKey = fullKey;
      t.stack.push_back(std::move(v));
    }
    break;
  }
  case 0x074: { // CountStrategy [varName]
    auto a = popArgs(t, args.size());
    std::string key = !args.empty() ? args[0] : "";
    if (!a.empty() && !a[0].lvalueKey.empty())
      key = a[0].lvalueKey;
    auto it = arrayKeys_.find(key);
    t.stack.push_back(NsbValue::makeInt(
        it != arrayKeys_.end() ? static_cast<std::int64_t>(it->second.size())
                               : 0));
    break;
  }
  case 0x075: { // VariableValue [sigil, name] or [sigil, name, value]
    auto a = popArgs(t, args.size());
    if (a.size() >= 3) {
      const std::string key = a[0].asString() + a[1].asString();
      vars_[key] = a[2];
      t.stack.push_back(a[2]);
    } else if (a.size() == 2) {
      const std::string key = a[0].asString() + a[1].asString();
      t.stack.push_back(readVar(key));
    } else if (a.size() == 1) {
      t.stack.push_back(readVar(a[0].asString()));
    } else {
      t.stack.push_back(NsbValue::makeInt(0));
    }
    break;
  }

  // --- String & Conversion Builtins ---
  case 0x001: { // SubStrStrategy [str, pos, len]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2) {
      const std::string s = a[0].asString();
      const auto pos =
          static_cast<std::size_t>(std::max<std::int64_t>(0, a[1].asInt()));
      const auto len = a.size() >= 3
                           ? static_cast<std::size_t>(
                                 std::max<std::int64_t>(0, a[2].asInt()))
                           : std::string::npos;
      t.stack.push_back(
          NsbValue::makeString(pos < s.size() ? s.substr(pos, len) : ""));
    } else {
      t.stack.push_back(NsbValue::makeString(""));
    }
    break;
  }
  case 0x063: { // StrlenStrategy
    auto a = popArgs(t, args.size());
    t.stack.push_back(NsbValue::makeInt(
        a.empty() ? 0 : static_cast<std::int64_t>(a[0].asString().size())));
    break;
  }
  case 0x064: { // StrStrStrategy [haystack, needle]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2) {
      const auto found = a[0].asString().find(a[1].asString());
      t.stack.push_back(NsbValue::makeInt(
          found == std::string::npos ? 0
                                     : static_cast<std::int64_t>(found + 1)));
    } else {
      t.stack.push_back(NsbValue::makeInt(0));
    }
    break;
  }
  case 0x065: { // StringStrategy [fmt, ...]
    auto a = popArgs(t, args.size());
    if (a.empty()) {
      t.stack.push_back(NsbValue::makeString(""));
    } else if (a.size() == 1) {
      t.stack.push_back(NsbValue::makeString(a[0].asString()));
    } else {
      // Minimal printf-style formatter (%d, %02d, %s)
      const std::string fmt = a[0].asString();
      std::string out;
      std::size_t argIdx = 1;
      for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size() && argIdx < a.size()) {
          std::size_t j = i + 1;
          int width = 0;
          bool zeroPad = false;
          if (j < fmt.size() && fmt[j] == '0') {
            zeroPad = true;
            ++j;
          }
          while (j < fmt.size() &&
                 std::isdigit(static_cast<unsigned char>(fmt[j]))) {
            width = width * 10 + (fmt[j] - '0');
            ++j;
          }
          if (j < fmt.size() && (fmt[j] == 'd' || fmt[j] == 'i')) {
            std::ostringstream oss;
            if (width > 0)
              oss << std::setw(width) << std::setfill(zeroPad ? '0' : ' ');
            oss << a[argIdx++].asInt();
            out += oss.str();
            i = j;
            continue;
          } else if (j < fmt.size() && fmt[j] == 's') {
            out += a[argIdx++].asString();
            i = j;
            continue;
          }
        }
        out.push_back(fmt[i]);
      }
      t.stack.push_back(NsbValue::makeString(std::move(out)));
    }
    break;
  }
  case 0x06e: { // IntegerStrategy
    auto a = popArgs(t, args.size());
    t.stack.push_back(NsbValue::makeInt(a.empty() ? 0 : a[0].asInt()));
    break;
  }

  // --- Control Flow ---
  case 0x094: // ChapterDeclaration
  case 0x095: // SceneDeclaration
  case 0x096: // CutDeclaration
  case 0x097: // FunctionDeclaration (only entered via CallFunction; if fallen
              // into, stop)
    break;
  case 0x098: { // IfStrategy [falseLabel]
    auto cond = popOne(t);
    if (!cond.isTruthy() && !args.empty()) {
      if (auto dst = t.script->findSymbol(args[0]); dst != std::string::npos)
        t.pc = dst;
    }
    break;
  }
  case 0x099: { // WhileStrategy [endLabel]
    auto cond = popOne(t);
    if (!args.empty()) {
      if (!cond.isTruthy()) {
        if (!t.whileEndStack.empty() && t.whileEndStack.back() == args[0])
          t.whileEndStack.pop_back();
        if (auto dst = t.script->findSymbol(args[0]); dst != std::string::npos)
          t.pc = dst;
      } else {
        if (t.whileEndStack.empty() || t.whileEndStack.back() != args[0])
          t.whileEndStack.push_back(args[0]);
      }
    }
    break;
  }
  case 0x0df: // WhileEndStrategy
    break;
  case 0x0cf: { // JumpStrategy [label]
    if (!args.empty()) {
      if (auto dst = t.script->findSymbol(args[0]); dst != std::string::npos)
        t.pc = dst;
    }
    break;
  }
  case 0x09d: { // BreakStrategy
    std::string target;
    if (!args.empty()) {
      target = args[0];
    } else if (!t.whileEndStack.empty()) {
      target = t.whileEndStack.back();
      t.whileEndStack.pop_back();
    } else if (!t.selectStack.empty()) {
      target = t.selectStack.back().endLabel;
      t.selectStack.pop_back();
    }
    if (!target.empty()) {
      if (auto dst = t.script->findSymbol(target); dst != std::string::npos)
        t.pc = dst;
    }
    break;
  }
  case 0x09c:   // ReturnStrategy
  case 0x0d4:   // EndChapterStrategy
  case 0x0d5:   // EndSceneStrategy
  case 0x0d6:   // EndCutStrategy
  case 0x0d7: { // EndFunctionStrategy
    if (!t.callStack.empty()) {
      auto frame = std::move(t.callStack.back());
      t.callStack.pop_back();
      // Function parameters are global N2 variables and deliberately survive
      // the return so later builtins can read them (S;G MovieWaitSG reads the
      // $MOVIESG entity name written by CreateMovieSG). Keep savedParams in
      // the frame for save-state fidelity, but never apply them on return.
      t.script = std::move(frame.script);
      t.pc = frame.pc;
    } else {
      t.running = false;
      yielded = true;
    }
    break;
  }
  case 0x0ce: { // CallFunctionStrategy [funcName, arg1, arg2, ...]
    if (args.empty())
      break;
    const std::size_t argCount = args.size() - 1;
    auto callArgs = popArgs(t, argCount);
    std::shared_ptr<NsbScript> targetScript;
    std::size_t targetPc = 0;
    if (resolveFunction(t.script, args[0], targetScript, targetPc) &&
        t.callStack.size() < 512) {
      CallFrame frame;
      frame.script = t.script;
      frame.pc = t.pc;
      const auto &declIns = targetScript->instructions()[targetPc];
      for (std::size_t p = 1; p < declIns.args.size(); ++p) {
        const std::string &paramName = declIns.args[p];
        auto it = vars_.find(paramName);
        if (it != vars_.end())
          frame.savedParams.push_back({paramName, {true, it->second}});
        else
          frame.savedParams.push_back(
              {paramName, {false, NsbValue::makeInt(0)}});
        vars_[paramName] =
            (p - 1 < callArgs.size()) ? callArgs[p - 1] : NsbValue::makeInt(0);
      }
      t.callStack.push_back(std::move(frame));
      t.script = std::move(targetScript);
      t.pc = targetPc + 1;
    }
    break;
  }
  case 0x0a2:   // CallChapterStrategy
  case 0x0a3:   // CallSceneStrategy
  case 0x0a4: { // CallCutStrategy
    if (args.empty())
      break;
    std::shared_ptr<NsbScript> targetScript;
    std::size_t targetPc = 0;
    if (resolveTarget(t.script, args[0], op == 0x0a3, targetScript, targetPc) &&
        t.callStack.size() < 512) {
      CallFrame frame;
      frame.script = t.script;
      frame.pc = t.pc;
      frame.isChapterOrScene = true;
      t.callStack.push_back(std::move(frame));
      t.script = std::move(targetScript);
      t.pc = targetPc + 1;
    }
    break;
  }

  // --- Select / Case ---
  case 0x09a: { // SelectStrategy [endLabel]
    activeChoices_.clear();
    SelectFrame sf;
    sf.selectPc = t.pc - 1;
    sf.endLabel = args.empty() ? "" : args[0];
    t.selectStack.push_back(std::move(sf));
    break;
  }
  case 0x09b: { // CaseStrategy [choiceName, caseEndLabel, caseStartLabel]
    if (args.size() >= 2) {
      const std::string &choiceName = args[0];
      if (!triggeredChoice_.empty() && triggeredChoice_ == choiceName) {
        triggeredChoice_.clear();
        activeChoices_.clear();
        // Fall through into case body!
      } else {
        if (std::find(activeChoices_.begin(), activeChoices_.end(),
                      choiceName) == activeChoices_.end()) {
          activeChoices_.push_back(choiceName);
        }
        if (auto dst = t.script->findSymbol(args[1]); dst != std::string::npos)
          t.pc = dst;
      }
    }
    break;
  }
  case 0x0dd: // CaseEndStrategy
    break;
  case 0x0e0:   // SelectEndStrategy
  case 0x101: { // SelectBreakEndStrategy
    if (!t.selectStack.empty()) {
      const auto sf = t.selectStack.back();
      if (!activeChoices_.empty()) {
        // Yield until user picks a choice, then loop back to SelectStrategy.
        t.pc = sf.selectPc;
        t.selectStack.pop_back();
        t.waitingChoice = true;
        yielded = true;
      } else {
        t.selectStack.pop_back();
      }
    }
    break;
  }

  // --- Visual Entities ---
  case 0x00a: { // CreateColor [name, prio, x, y, w, h, color]
    auto a = popArgs(t, args.size());
    if (a.size() >= 7) {
      auto *e = sceneGraph_.createEntity(a[0].asString(), EntityType::Color);
      e->z = static_cast<float>(a[1].asFloat());
      e->x = resolveCoord(a[2], 0.0f, screenW, true);
      e->y = resolveCoord(a[3], 0.0f, screenH, false);
      e->width = static_cast<float>(a[4].asFloat());
      e->height = static_cast<float>(a[5].asFloat());
      e->solidColor = parseColorName(a[6].asString());
      e->opacity = 1.0f;
      e->visible = true;
      sceneGraph_.setPriority(e->name, e->z);
    }
    break;
  }
  case 0x00d: { // CreateTexture [name, prio, x, y, path]
    auto a = popArgs(t, args.size());
    if (a.size() >= 5) {
      auto *e = sceneGraph_.createEntity(a[0].asString(), EntityType::Sprite);
      e->z = static_cast<float>(a[1].asFloat());
      e->x = resolveCoord(a[2], 0.0f, screenW, true);
      e->y = resolveCoord(a[3], 0.0f, screenH, false);
      applyAlignmentFlags(*e, a[2], a[3]);
      e->imagePath = normalizeAssetPath(a[4].asString());
      const bool hiddenState = isHoverOnlyChoiceChild(e->name);
      e->opacity = hiddenState ? 0.0f : 1.0f;
      e->visible = !hiddenState;
      e->hasClipRect = false;
      sceneGraph_.setPriority(e->name, e->z);
    }
    break;
  }
  case 0x00e: { // CreateClipTexture [name, prio, x, y, ox, oy, w, h, path]
    auto a = popArgs(t, args.size());
    if (a.size() >= 9) {
      auto *e = sceneGraph_.createEntity(a[0].asString(), EntityType::Sprite);
      e->z = static_cast<float>(a[1].asFloat());
      e->x = resolveCoord(a[2], 0.0f, screenW, true);
      e->y = resolveCoord(a[3], 0.0f, screenH, false);
      applyAlignmentFlags(*e, a[2], a[3]);
      e->hasClipRect = true;
      e->clipX = static_cast<float>(a[4].asFloat());
      e->clipY = static_cast<float>(a[5].asFloat());
      e->clipW = static_cast<float>(a[6].asFloat());
      e->clipH = static_cast<float>(a[7].asFloat());
      e->imagePath = normalizeAssetPath(a[8].asString());
      const bool hiddenState = isHoverOnlyChoiceChild(e->name);
      e->opacity = hiddenState ? 0.0f : 1.0f;
      e->visible = !hiddenState;
      sceneGraph_.setPriority(e->name, e->z);
    }
    break;
  }
  case 0x018: { // CreateScrollBar [name, prio, x1, y1, x2, y2, val, dir, ...]
    auto a = popArgs(t, args.size());
    if (!a.empty()) {
      auto *e =
          sceneGraph_.createEntity(a[0].asString(), EntityType::ScrollBar);
      if (a.size() >= 2)
        e->z = static_cast<float>(a[1].asFloat());
      if (a.size() >= 6) {
        const float x1 = resolveCoord(a[2], 0.0f, screenW, true);
        const float y1 = resolveCoord(a[3], 0.0f, screenH, false);
        const float x2 = static_cast<float>(a[4].asFloat());
        const float y2 = static_cast<float>(a[5].asFloat());
        e->x = x1;
        e->y = y1;
        e->width = (x2 > x1) ? (x2 - x1) : std::max(16.0f, x2);
        e->height = (y2 > y1) ? (y2 - y1) : std::max(16.0f, y2);
        e->scrollHorizontal = e->width >= e->height;
      }
      if (a.size() >= 7)
        e->scrollValue =
            std::clamp(static_cast<float>(a[6].asFloat()), 0.0f, 100.0f);
      e->visible = true;
      e->opacity = 1.0f;
      sceneGraph_.setPriority(e->name, e->z);
    }
    break;
  }
  case 0x006:   // CreateStencil
  case 0x007:   // CreateSurface
  case 0x008:   // CreateName
  case 0x00b:   // CreateEffect
  case 0x00f:   // CreateMask
  case 0x015:   // CreateCamera
  case 0x017: { // CreateBacklog
    auto a = popArgs(t, args.size());
    if (!a.empty()) {
      auto *e =
          sceneGraph_.createEntity(a[0].asString(), EntityType::Container);
      if (a.size() >= 2)
        sceneGraph_.setPriority(e->name, static_cast<float>(a[1].asFloat()));
    }
    break;
  }
  case 0x014: { // CreateWindow [name, prio, x, y, w, h, ...]
    auto a = popArgs(t, args.size());
    if (a.size() >= 6) {
      auto *e = sceneGraph_.createEntity(a[0].asString(), EntityType::Window);
      e->z = static_cast<float>(a[1].asFloat());
      e->x = resolveCoord(a[2], 0.0f, screenW, true);
      e->y = resolveCoord(a[3], 0.0f, screenH, false);
      e->width = static_cast<float>(a[4].asFloat());
      e->height = static_cast<float>(a[5].asFloat());
      sceneGraph_.setPriority(e->name, e->z);
    }
    break;
  }
  case 0x013: { // CreateText [name, prio, x, y, w, h, text]
    auto a = popArgs(t, args.size());
    if (a.size() >= 7) {
      auto *e = sceneGraph_.createEntity(a[0].asString(), EntityType::Text);
      e->z = static_cast<float>(a[1].asFloat());
      e->x = resolveCoord(a[2], 0.0f, screenW, true);
      e->y = resolveCoord(a[3], 0.0f, screenH, false);
      applyAlignmentFlags(*e, a[2], a[3]);
      e->width = static_cast<float>(a[4].asFloat());
      e->height = static_cast<float>(a[5].asFloat());
      e->text = a[6].asString();
      e->visible = true;
      sceneGraph_.setPriority(e->name, e->z);
    }
    break;
  }
  case 0x016: { // CreateChoice [name]
    auto a = popArgs(t, args.size());
    if (!a.empty()) {
      sceneGraph_.createEntity(a[0].asString(), EntityType::Choice);
    }
    break;
  }
  case 0x012: { // CreateMovie [name, prio, x, y, loop, alpha, path, audio]
    auto a = popArgs(t, args.size());
    // CreateMovieSG passes bare parameter names ("描画優先度", "Ｙ座標",
    // "ループ", "音声同期") as literal strings. Resolve every operand against
    // the global variable slots, otherwise priority becomes 0 and the movie
    // renders behind BGBUF1 (BG_BLACK.jpg, z=100) - a black intro screen.
    for (auto &arg : a)
      arg = resolveVariableName(arg);
    if (a.size() >= 7) {
      auto *e = sceneGraph_.createEntity(a[0].asString(), EntityType::Movie);
      e->z = static_cast<float>(a[1].asFloat());
      e->x = resolveCoord(a[2], 0.0f, screenW, true);
      e->y = resolveCoord(a[3], 0.0f, screenH, false);
      e->movieLoop = a[4].isTruthy();
      e->moviePath = normalizeAssetPath(a[6].asString());
      e->moviePlaying = true;
      e->visible = true;
      sceneGraph_.setPriority(e->name, e->z);
      activeMovie_.entityName = e->name;
      activeMovie_.path = e->moviePath;
      activeMovie_.loop = e->movieLoop;
      activeMovie_.fullscreen = false;
      activeMovie_.active = true;
    }
    break;
  }
  case 0x04b: { // MoviePlay [path, skip]
    auto a = popArgs(t, args.size());
    if (!a.empty()) {
      activeMovie_.entityName = "__fullscreen_movie__";
      activeMovie_.path = normalizeAssetPath(a[0].asString());
      activeMovie_.loop = false;
      activeMovie_.fullscreen = true;
      activeMovie_.active = true;
      t.waitingMovie = true;
      yielded = true;
    }
    break;
  }
  case 0x11b: { // SetMovieClipRect [name, x, y, w, h]
    auto a = popArgs(t, args.size());
    if (a.size() >= 5) {
      if (auto *e = sceneGraph_.findEntity(a[0].asString())) {
        e->hasClipRect = true;
        e->clipX = static_cast<float>(a[1].asFloat());
        e->clipY = static_cast<float>(a[2].asFloat());
        e->clipW = static_cast<float>(a[3].asFloat());
        e->clipH = static_cast<float>(a[4].asFloat());
      }
    }
    break;
  }
  case 0x019:   // LoadImage
  case 0x01b:   // LoadColor
  case 0x01c:   // LoadFont
  case 0x01d:   // LoadText
  case 0x01e:   // LoadFile
  case 0x01f: { // UnLoadFile
    auto a = popArgs(t, args.size());
    if (op == 0x019 && a.size() >= 2) {
      // LoadImage fills an EXISTING entity created by CreateTexture. Creating
      // a fresh one here discarded its transform/priority/visibility and made
      // character sprites disappear (e.g. Mayuri in function.nsb).
      auto *e = sceneGraph_.findEntity(a[0].asString());
      const bool fresh = !e || e->type != EntityType::Sprite;
      if (fresh)
        e = sceneGraph_.createEntity(a[0].asString(), EntityType::Sprite);
      e->imagePath = normalizeAssetPath(a[1].asString());
      if (fresh)
        e->visible = false;
    } else if (op == 0x01b && a.size() >= 4) {
      auto *e = sceneGraph_.findEntity(a[0].asString());
      if (!e || e->type != EntityType::Color)
        e = sceneGraph_.createEntity(a[0].asString(), EntityType::Color);
      e->width = static_cast<float>(a[1].asFloat());
      e->height = static_cast<float>(a[2].asFloat());
      e->solidColor = parseColorName(a[3].asString());
    }
    break;
  }
  case 0x002: { // SetPrio [name, prio]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2)
      sceneGraph_.setPriority(a[0].asString(),
                              static_cast<float>(a[1].asFloat()));
    break;
  }
  case 0x003: { // SetColor [name, color]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2) {
      for (auto *e : sceneGraph_.queryEntities(a[0].asString()))
        e->solidColor = parseColorName(a[1].asString());
    }
    break;
  }
  case 0x02b: { // SetAlias [entity, alias]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2)
      sceneGraph_.setAlias(a[0].asString(), a[1].asString());
    break;
  }
  case 0x02d: { // SetShade [query, Medium|Heavy]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2) {
      const std::string query = a[0].asString();
      const std::string level = toLowerAscii(a[1].asString());
      // NitroSharp's Medium/Heavy shader presets use 8/16 blur passes.
      const unsigned passes = level == "heavy" || level == "16"   ? 16u
                              : level == "medium" || level == "8" ? 8u
                                                                  : 0u;
      sceneGraph_.setBoxBlur(query, passes);
    }
    break;
  }
  case 0x02e: { // SetTone [query, Monochrome]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2) {
      const std::string query = a[0].asString();
      const std::string tone = toLowerAscii(a[1].asString());
      sceneGraph_.setMonochrome(query, tone == "monochrome" || tone == "mono" ||
                                           tone == "1");
    }
    break;
  }
  case 0x034: { // SetNextFocus [src, dst, dir]
    auto a = popArgs(t, args.size());
    if (a.size() >= 3) {
      if (auto *e = sceneGraph_.findEntity(a[0].asString())) {
        const auto dir = toLowerAscii(a[2].asString());
        if (dir == "up")
          e->focusUp = a[1].asString();
        else if (dir == "down")
          e->focusDown = a[1].asString();
        else if (dir == "left")
          e->focusLeft = a[1].asString();
        else if (dir == "right")
          e->focusRight = a[1].asString();
      }
    }
    break;
  }
  case 0x020: { // Move [query, ms, x, y, tempo, wait]
    auto a = popArgs(t, args.size());
    if (a.size() >= 4) {
      const std::string query = a[0].asString();
      const float ms = static_cast<float>(a[1].asFloat());
      for (auto *e : sceneGraph_.queryEntities(query)) {
        const float tx = resolveCoord(a[2], e->x, screenW, true);
        const float ty = resolveCoord(a[3], e->y, screenH, false);
        sceneGraph_.moveEntity(e->name, tx, ty, ms,
                               a.size() >= 5
                                   ? parseEaseFunction(a[4].asString())
                                   : EaseFunction::Linear);
      }
      if (a.size() >= 6 && a[5].isTruthy() && ms > 0.0f) {
        t.waitEntity = query;
        t.waitEntityAnim = true;
        yielded = true;
      }
    }
    break;
  }
  case 0x021: { // BezierMove [query,ms,c1x,c1y,c2x,c2y,x,y,tempo,wait]
    auto a = popArgs(t, args.size());
    if (a.size() >= 8) {
      const std::string query = a[0].asString();
      const float ms = static_cast<float>(a[1].asFloat());
      sceneGraph_.bezierMoveEntity(query, static_cast<float>(a[2].asFloat()),
                                   static_cast<float>(a[3].asFloat()),
                                   static_cast<float>(a[4].asFloat()),
                                   static_cast<float>(a[5].asFloat()),
                                   static_cast<float>(a[6].asFloat()),
                                   static_cast<float>(a[7].asFloat()), ms,
                                   a.size() >= 9
                                       ? parseEaseFunction(a[8].asString())
                                       : EaseFunction::Linear);
      if (a.size() >= 10 && a[9].isTruthy() && ms > 0.0f) {
        t.waitEntity = query;
        t.waitEntityAnim = true;
        yielded = true;
      }
    } else if (a.size() >= 4) {
      // Legacy native bytecode only provides a final coordinate pair.
      sceneGraph_.moveEntity(a[0].asString(),
                             static_cast<float>(a[2].asFloat()),
                             static_cast<float>(a[3].asFloat()),
                             static_cast<float>(a[1].asFloat()));
    }
    break;
  }
  case 0x025: { // Fade [query, ms, opacity(0..1000), tempo, wait]
    auto a = popArgs(t, args.size());
    if (a.size() >= 3) {
      const std::string query = a[0].asString();
      const float ms = static_cast<float>(a[1].asFloat());
      const float opVal =
          std::clamp(static_cast<float>(a[2].asFloat()) / 1000.0f, 0.0f, 1.0f);
      sceneGraph_.fadeEntity(query, opVal, ms,
                             a.size() >= 4 ? parseEaseFunction(a[3].asString())
                                           : EaseFunction::Linear);
      if (a.size() >= 5 && a[4].isTruthy() && ms > 0.0f) {
        t.waitEntity = query;
        t.waitEntityAnim = true;
        yielded = true;
      }
    }
    break;
  }
  case 0x026: { // Zoom [query, ms, sx(0..1000), sy(0..1000), tempo, wait]
    auto a = popArgs(t, args.size());
    if (a.size() >= 4) {
      const std::string query = a[0].asString();
      const float ms = static_cast<float>(a[1].asFloat());
      const float sx = static_cast<float>(a[2].asFloat()) / 1000.0f;
      const float sy = static_cast<float>(a[3].asFloat()) / 1000.0f;
      sceneGraph_.zoomEntity(query, sx, sy, ms,
                             a.size() >= 5 ? parseEaseFunction(a[4].asString())
                                           : EaseFunction::Linear);
      if (a.size() >= 6 && a[5].isTruthy() && ms > 0.0f) {
        t.waitEntity = query;
        t.waitEntityAnim = true;
        yielded = true;
      }
    }
    break;
  }
  case 0x027: { // Rotate [query, ms, rx, ry, rz, tempo, wait]
    auto a = popArgs(t, args.size());
    if (a.size() >= 5) {
      const std::string query = a[0].asString();
      const float ms = static_cast<float>(a[1].asFloat());
      const float rz = static_cast<float>(a[4].asFloat());
      sceneGraph_.rotateEntity(query, rz, ms,
                               a.size() >= 6
                                   ? parseEaseFunction(a[5].asString())
                                   : EaseFunction::Linear);
    }
    break;
  }
  case 0x028: { // ShakeMove [query, ms, ax, ay, ...]
    auto a = popArgs(t, args.size());
    if (a.size() >= 4) {
      const std::string query = a[0].asString();
      const float ms = static_cast<float>(a[1].asFloat());
      const float ax = static_cast<float>(a[2].asFloat());
      const float ay = static_cast<float>(a[3].asFloat());
      sceneGraph_.shakeEntity(query, ax, ay, ms);
    }
    break;
  }
  case 0x038: { // DrawTransition [query, ms, start(0..1000), end(0..1000),
                // fuzzy, tempo, mask, wait]
    auto a = popArgs(t, args.size());
    if (a.size() >= 4) {
      const std::string query = a[0].asString();
      const float ms = static_cast<float>(a[1].asFloat());
      const float s =
          std::clamp(static_cast<float>(a[2].asFloat()) / 1000.0f, 0.0f, 1.0f);
      const float e =
          std::clamp(static_cast<float>(a[3].asFloat()) / 1000.0f, 0.0f, 1.0f);
      const std::string mask =
          a.size() >= 7 ? normalizeAssetPath(a[6].asString()) : "";
      sceneGraph_.transitionEntity(query, s, e, ms, mask,
                                   a.size() >= 6
                                       ? parseEaseFunction(a[5].asString())
                                       : EaseFunction::Linear);
      if (a.size() >= 8 && a[7].isTruthy() && ms > 0.0f) {
        t.waitEntity = query;
        t.waitEntityAnim = true;
        yielded = true;
      }
    }
    break;
  }
  case 0x024: { // Delete [query]
    auto a = popArgs(t, args.size());
    if (!a.empty()) {
      const std::string query = a[0].asString();
      sceneGraph_.destroyMatching(query, true);
      // Only mark for deletion. Erasing the current Thread from a vector
      // or deque while execInstruction has its reference is undefined.
      for (std::size_t i = 1; i < threads_.size(); ++i) {
        if (!threads_[i].locked &&
            SceneGraph::wildcardMatch(threads_[i].name, query)) {
          threads_[i].deleted = true;
          threads_[i].running = false;
        }
      }
    }
    break;
  }
  case 0x009: { // CreateProcess [name, prio, x, y, funcName]
    auto a = popArgs(t, args.size());
    if (a.size() >= 5) {
      const std::string procName = a[0].asString();
      const std::string funcName = a[4].asString();
      std::shared_ptr<NsbScript> fnScript;
      std::size_t fnPc = 0;
      if (resolveFunction(t.script, funcName, fnScript, fnPc)) {
        // NitroSharp replaces duplicate process names. Defer destruction
        // until the current VM instruction has returned.
        for (std::size_t i = 1; i < threads_.size(); ++i) {
          if (threads_[i].name == procName) {
            threads_[i].deleted = true;
            threads_[i].running = false;
          }
        }
        Thread proc;
        proc.name = procName;
        proc.script = fnScript;
        proc.pc = fnPc + 1;
        proc.running = false; // Started via Request(name, "Start")
        threads_.push_back(std::move(proc));
      }
    }
    break;
  }
  case 0x023: { // Request [query, action]
    auto a = popArgs(t, args.size());
    if (a.size() >= 2) {
      const std::string query = a[0].asString();
      const std::string action = toLowerAscii(a[1].asString());
      for (auto *e : sceneGraph_.queryEntities(query)) {
        if (action == "lock")
          e->locked = true;
        else if (action == "unlock")
          e->locked = false;
        else if (action == "smoothing")
          e->smoothing = true;
        else if (action == "hide" || action == "erase")
          e->visible = false;
        else if (action == "show" || action == "enter" ||
                 action == "fadeenter" || action == "open") {
          // The original Request dispatcher (STEINSGATE.exe 0x47b740)
          // recognises FadeEnter/Open in addition to Show/Enter. The host
          // compositor owns opacity transitions, so make the entity visible
          // here and let Fade/Zoom opcodes animate it when the script asks.
          e->visible = true;
          e->opacity = 1.0f;
        } else if (action == "nolog" || action == "nolg") {
          // Request(..., "NoLog") suppresses backlog for the next line; the
          // host owns backlog, so acknowledge it without changing the entity.
        } else if (action == "stop")
          e->moviePlaying = false;
        else if (action == "play" || action == "start") {
          if (e->type == EntityType::Movie) {
            e->moviePlaying = true;
            activeMovie_.entityName = e->name;
            activeMovie_.path = e->moviePath;
            activeMovie_.loop = e->movieLoop;
            activeMovie_.active = true;
          }
        }
      }
      // Check background processes
      for (std::size_t i = 1; i < threads_.size(); ++i) {
        if (!threads_[i].deleted &&
            SceneGraph::wildcardMatch(threads_[i].name, query)) {
          if (action == "start" || action == "resume")
            threads_[i].running = true;
          else if (action == "stop" || action == "pause")
            threads_[i].running = false;
          else if (action == "lock")
            threads_[i].locked = true;
          else if (action == "unlock")
            threads_[i].locked = false;
        }
      }
      // Check sound entities. CreateSound only *creates* a muted entity in
      // the original engine; Request(Play) is what actually starts it. The
      // retail title BGM is started this way by function.SoundPlay, so BGM
      // must never be started merely by CreateSound (nor stopped by
      // "Disused", which means destroy-when-idle, not Stop).
      for (const auto &sound : matchingSoundEntities(query)) {
        const auto &type = soundTypes_[sound];
        const auto &path = soundEntities_[sound];
        if (action == "play" || action == "start") {
          const float volume = soundVolumes_.count(sound)
                                   ? soundVolumes_[sound]
                                   : -1.0f;
          const bool loop =
              soundLoops_.count(sound) ? soundLoops_[sound] : true;
          AudioChannelKind kind = AudioChannelKind::Bgm;
          if (type == "BGM") {
            audioSystem_.playBgm(path, loop);
          } else if (type == "VOICE") {
            audioSystem_.playVoice(path);
            kind = AudioChannelKind::Voice;
          } else {
            audioSystem_.playSe(path, 0, loop);
            kind = AudioChannelKind::Se;
          }
          if (volume >= 0.0f)
            audioSystem_.fadeVolume(kind, volume, 0.0f);
        } else if (action == "stop") {
          // A wildcard query must not kill an unrelated track that is still
          // playing; only stop when this entity owns the active channel.
          if (type == "BGM" &&
              audioSystem_.bgmChannel().assetPath == path)
            audioSystem_.stopBgm();
          else if (type == "VOICE" &&
                   audioSystem_.voiceChannel().assetPath == path)
            audioSystem_.stopVoice();
        }
      }
      // Check <PRE> text blocks
      if (auto pit = preBlocks_.find(query); pit != preBlocks_.end()) {
        if (action == "enter" || action == "start" || action == "push" ||
            action == "show") {
          dialogueSystem_.setDialogue(pit->second.speaker, pit->second.text,
                                      pit->second.voicePath);
          if (!pit->second.voicePath.empty())
            audioSystem_.playVoice(pit->second.voicePath);
        }
      }
    }
    break;
  }

  // --- Audio ---
  case 0x011: { // CreateSound [name, type(BGM/SE/VOICE), path]
    auto a = popArgs(t, args.size());
    if (a.size() >= 3) {
      std::string path = normalizeAssetPath(a[2].asString());
      if (path.size() < 4 || path.compare(path.size() - 4, 4, ".ogg") != 0)
        path += ".ogg";
      const std::string name = a[0].asString();
      const std::string type = a[1].asString();
      soundEntities_[name] = path;
      soundTypes_[name] = type;
      // InitBGM creates ~70 muted BGM entities at boot. Playing all of them
      // here left the last one (bgm99st.ogg) on the single BGM channel with
      // volume 0, which is exactly the "title music is silent" regression.
      soundVolumes_[name] = -1.0f;
      soundLoops_[name] = true;
    }
    break;
  }
  case 0x043: { // SetVolume [query, ms, vol(0..1000), tempo]
    auto a = popArgs(t, args.size());
    // The NSB compiler emits bare parameter names ("秒数", "ボリウム") as
    // literal strings inside function bodies; resolve them to the values the
    // caller supplied before interpreting the operands.
    for (auto &arg : a)
      arg = resolveVariableName(arg);
    if (a.size() >= 3) {
      const float vol =
          std::clamp(static_cast<float>(a[2].asFloat()) / 1000.0f, 0.0f, 1.0f);
      // Operand 1 is the fade duration in milliseconds; the original engine
      // crossfades volume changes instead of jumping to the target instantly.
      const float fadeMs =
          a.size() > 1 ? std::max(0.0f, static_cast<float>(a[1].asFloat()))
                       : 0.0f;
      const auto &q = a[0].asString();
      for (const auto &sound : matchingSoundEntities(q)) {
        soundVolumes_[sound] = vol;
        const auto &type = soundTypes_[sound];
        const auto &path = soundEntities_[sound];
        AudioChannelKind kind = AudioChannelKind::Bgm;
        if (type == "BGM") {
          if (audioSystem_.bgmChannel().assetPath != path ||
              !audioSystem_.bgmChannel().isPlaying)
            continue;
        } else if (type == "VOICE") {
          if (audioSystem_.voiceChannel().assetPath != path ||
              !audioSystem_.voiceChannel().isPlaying)
            continue;
          kind = AudioChannelKind::Voice;
        } else {
          if (audioSystem_.seChannels().empty())
            continue;
          kind = AudioChannelKind::Se;
        }
        // Only a change to the currently playing entity affects the channel.
        audioSystem_.fadeVolume(kind, vol, fadeMs);
      }
    }
    break;
  }
  case 0x044: // SetPan
  case 0x045: // SetFrequency
  case 0x047: // SetLoopPoint
  case 0x06f: // SoundAmplitude
    (void)popArgs(t, args.size());
    if (op == 0x06f)
      t.stack.push_back(NsbValue::makeInt(0));
    break;
  case 0x046: { // SetLoop [query, loop]
    auto a = popArgs(t, args.size());
    for (auto &arg : a)
      arg = resolveVariableName(arg);
    if (a.size() >= 2) {
      const bool loop = resolveVariableName(a[1]).isTruthy();
      for (const auto &sound : matchingSoundEntities(a[0].asString())) {
        soundLoops_[sound] = loop;
        if (soundTypes_[sound] == "BGM" &&
            audioSystem_.bgmChannel().assetPath == soundEntities_[sound])
          audioSystem_.setBgmLoop(loop);
      }
    }
    break;
  }

  // --- Dialogue & <PRE> ---
  case 0x0d8: { // PreCreateStrategy [textId, boxName, rawXml]
    if (args.size() >= 3) {
      auto block = parsePreBlock(args[0], args[1], args[2]);
      vars_["$SYSTEM_present_text"] = NsbValue::makeString(block.id);
      vars_["$SYSTEM_last_text"] = NsbValue::makeString(block.text);
      if (!block.text.empty()) {
        dialogueSystem_.setDialogue(block.speaker, block.text, block.voicePath);
        if (!block.voicePath.empty())
          audioSystem_.playVoice(block.voicePath);
      }
      preBlocks_[block.id] = std::move(block);
    }
    break;
  }
  case 0x042: { // WaitText [textName, timeout]
    (void)popArgs(t, args.size());
    t.waitingText = true;
    yielded = true;
    break;
  }

  // --- Waits & Timers ---
  case 0x039: { // Wait [ms]
    auto a = popArgs(t, args.size());
    if (!a.empty()) {
      const float ms = static_cast<float>(a[0].asFloat());
      if (ms > 0.0f) {
        t.waitTimer = ms / 1000.0f;
        yielded = true;
      }
    }
    break;
  }
  case 0x03a: { // WaitKey [optionalTimeoutMs]
    auto a = popArgs(t, args.size());
    t.waitingKey = true;
    t.waitKeyTimeout = (!a.empty() && a[0].asFloat() > 0.0)
                           ? static_cast<float>(a[0].asFloat() / 1000.0)
                           : -1.0f;
    yielded = true;
    break;
  }
  case 0x03b:   // WaitMove
  case 0x03c:   // WaitFade
  case 0x03d:   // WaitZoom
  case 0x03e:   // WaitRotate
  case 0x03f:   // WaitShake
  case 0x040: { // WaitAction [query, optionalTimeout]
    auto a = popArgs(t, args.size());
    if (!a.empty()) {
      bool anyBusy = false;
      for (auto *e : sceneGraph_.queryEntities(a[0].asString())) {
        if (e->isAnimating()) {
          anyBusy = true;
          break;
        }
      }
      if (anyBusy) {
        t.waitEntity = a[0].asString();
        t.waitEntityAnim = true;
        yielded = true;
      }
    }
    break;
  }
  case 0x041: { // WaitPlay [query, optionalTimeout]
    (void)popArgs(t, args.size());
    if (activeMovie_.active) {
      t.waitingMovie = true;
      yielded = true;
    }
    break;
  }
  case 0x06c: { // TimeStrategy: seconds elapsed in the process
    (void)popArgs(t, args.size());
    t.stack.push_back(NsbValue::makeInt(
        static_cast<std::int64_t>(std::max(0.0, elapsedSeconds_))));
    break;
  }
  case 0x06d: { // MilliTimeStrategy
    (void)popArgs(t, args.size());
    t.stack.push_back(
        NsbValue::makeInt(static_cast<std::int64_t>(elapsedSeconds_ * 1000.0)));
    break;
  }
  case 0x05e:   // RemainTime
  case 0x05f:   // DurationTime
  case 0x060: { // PassageTime
    auto a = popArgs(t, args.size());
    const std::string query =
        !a.empty() ? a[0].asString() : (args.empty() ? "" : args[0]);
    const MediaTimeKind kind = op == 0x05e   ? MediaTimeKind::Remaining
                               : op == 0x05f ? MediaTimeKind::Duration
                                             : MediaTimeKind::Elapsed;
    const auto result =
        mediaTimeQuery_ ? mediaTimeQuery_(query, kind) : std::int64_t{0};
    t.stack.push_back(NsbValue::makeInt(std::max<std::int64_t>(0, result)));
    break;
  }

  // --- System, Queries, Save/Load ---
  case 0x068: { // GetModuleFileNameStrategy -> basename e.g. "sg01_01.nss"
    (void)popArgs(t, args.size());
    std::string p = t.script ? t.script->path() : "";
    if (const auto slash = p.rfind('/'); slash != std::string::npos)
      p = p.substr(slash + 1);
    if (p.size() >= 4 && p.compare(p.size() - 4, 4, ".nsb") == 0)
      p.replace(p.size() - 4, 4, ".nss");
    t.stack.push_back(NsbValue::makeString(std::move(p)));
    break;
  }
  case 0x05d: { // GetRandom [max]
    auto a = popArgs(t, args.size());
    rngState_ = rngState_ * 1664525u + 1013904223u;
    const auto maxVal = (!a.empty() && a[0].asInt() > 0) ? a[0].asInt() : 100;
    t.stack.push_back(
        NsbValue::makeInt(static_cast<std::int64_t>(rngState_ % maxVal)));
    break;
  }
  case 0x061:   // GetImageHorizon
  case 0x062: { // GetImageVertical
    auto a = popArgs(t, args.size());
    int w = config_.width, h = config_.height;
    if (!a.empty() && imageSizeFn_) {
      std::string q = a[0].asString();
      if (auto *e = sceneGraph_.findEntity(q); e && !e->imagePath.empty())
        q = e->imagePath;
      (void)imageSizeFn_(q, w, h);
    }
    t.stack.push_back(NsbValue::makeInt(op == 0x061 ? w : h));
    break;
  }
  case 0x04f:   // Save(slot)
  case 0x050:   // Load(slot)
  case 0x051: { // DeleteSaveFile(slot)
    const auto values = popArgs(t, args.size());
    const auto slot = values.empty() ? std::int64_t{0} : values[0].asInt();
    if (slot >= 0 && slot <= std::numeric_limits<std::uint32_t>::max()) {
      const auto kind = op == 0x04f   ? SaveActionKind::Save
                        : op == 0x050 ? SaveActionKind::Load
                                      : SaveActionKind::Delete;
      pendingSaveAction_ =
          PendingSaveAction{kind, static_cast<std::uint32_t>(slot)};
      yielded = true; // Runtime executes action after stepThread unwinds.
    }
    break;
  }
  case 0x052: { // ExistSave [slot]
    auto a = popArgs(t, args.size());
    const auto slot =
        !a.empty()
            ? a[0].asInt()
            : (!args.empty() ? NsbValue::makeString(args[0]).asInt() : 0);
    const bool found = saveExistsQuery_ && slot >= 0 &&
                       slot <= std::numeric_limits<std::uint32_t>::max() &&
                       saveExistsQuery_(static_cast<std::uint32_t>(slot));
    t.stack.push_back(NsbValue::makeInt(found ? 1 : 0));
    break;
  }
  case 0x054: { // GetMousePosition
    (void)popArgs(t, args.size());
    t.stack.push_back(NsbValue::makeInt(mouseX_));
    break;
  }
  case 0x056: { // DateTime [year,month,day,hour,minute,second refs]
    const auto a = popArgs(t, args.size());
    if (args.size() >= 6) {
      const std::time_t now = std::time(nullptr);
      const std::tm *local = std::localtime(&now);
      if (local) {
        const int values[6] = {local->tm_year + 1900, local->tm_mon + 1,
                               local->tm_mday,        local->tm_hour,
                               local->tm_min,         local->tm_sec};
        for (std::size_t i = 0; i < 6; ++i) {
          const std::string name = i < a.size() && !a[i].lvalueKey.empty()
                                       ? a[i].lvalueKey
                                       : args[i];
          if (!name.empty())
            assignVar(t, name, NsbValue::makeInt(values[i]));
        }
      }
    }
    break;
  }
  case 0x077: { // PlatformStrategy
    (void)popArgs(t, args.size());
    t.stack.push_back(NsbValue::makeInt(0)); // PC
    break;
  }
  case 0x05a: { // ExitStrategy
    (void)popArgs(t, args.size());
    finished_ = true;
    t.running = false;
    yielded = true;
    break;
  }
  case 0x05b: { // ResetStrategy (re-enters start script in original; no-op
                // in-scene if $GameStart)
    (void)popArgs(t, args.size());
    break;
  }

  // --- Steins;Gate Phone Trigger & Mail Opcodes (0x0e3..0x11d) ---
  case 0x0e4: { // PhoneInit
    (void)popArgs(t, args.size());
    phone_.initialized = true;
    break;
  }
  case 0x0f7: { // AddressBookResetAll
    (void)popArgs(t, args.size());
    phone_.addressBook.clear();
    break;
  }
  case 0x0f8: { // AddressBookSet [id]
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.addressBook.insert(a[0].asString());
    break;
  }
  case 0x0f9: { // AddressBookReset [id]
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.addressBook.erase(a[0].asString());
    break;
  }
  case 0x10f: { // CheckRegistAddressId [id]
    auto a = popArgs(t, args.size());
    const bool ok = !a.empty() && phone_.addressBook.count(a[0].asString()) > 0;
    t.stack.push_back(NsbValue::makeInt(ok ? 1 : 0));
    break;
  }
  case 0x0eb: { // SetFlagMailReceive
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailReceiveFlags.insert(a[0].asString());
    break;
  }
  case 0x0ec: { // ResetFlagMailReceive
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailReceiveFlags.erase(a[0].asString());
    break;
  }
  case 0x0ed: { // SetFlagMailView
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailViewFlags.insert(a[0].asString());
    break;
  }
  case 0x0ee: { // ResetFlagMailView
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailViewFlags.erase(a[0].asString());
    break;
  }
  case 0x0ef: { // SetFlagMailReply
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailReplyFlags.insert(a[0].asString());
    break;
  }
  case 0x0f0: { // ResetFlagMailReply
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailReplyFlags.erase(a[0].asString());
    break;
  }
  case 0x0f1: { // SetFlagMailTrigger
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailTriggerFlags.insert(a[0].asString());
    break;
  }
  case 0x0f2: { // ResetFlagMailTrigger
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailTriggerFlags.erase(a[0].asString());
    break;
  }
  case 0x0f3: { // SetFlagMailReplySend
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailReplySendFlags.insert(a[0].asString());
    break;
  }
  case 0x0f4: { // ResetFlagMailReplySend
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.mailReplySendFlags.erase(a[0].asString());
    break;
  }
  case 0x118: { // ResetFlagAllMail
    (void)popArgs(t, args.size());
    phone_.mailReceiveFlags.clear();
    phone_.mailViewFlags.clear();
    phone_.mailReplyFlags.clear();
    phone_.mailTriggerFlags.clear();
    phone_.mailReplySendFlags.clear();
    break;
  }
  case 0x102:   // FlagOnJumpMailReceive
  case 0x103:   // FlagOffJumpMailReceive
  case 0x104:   // FlagOnJumpMailView
  case 0x105:   // FlagOffJumpMailView
  case 0x106:   // FlagOnJumpMailReply
  case 0x107:   // FlagOffJumpMailReply
  case 0x108:   // FlagOnJumpMailTrigger
  case 0x109:   // FlagOffJumpMailTrigger
  case 0x10a:   // FlagOnJumpMailReplySend
  case 0x10b: { // FlagOffJumpMailReplySend
    auto a = popArgs(t, args.size());
    const std::string key = a.empty() ? "" : a[0].asString();
    const bool wantOn = (op % 2 == 0);
    bool isOn = false;
    if (op == 0x102 || op == 0x103)
      isOn = phone_.mailReceiveFlags.count(key) > 0;
    else if (op == 0x104 || op == 0x105)
      isOn = phone_.mailViewFlags.count(key) > 0;
    else if (op == 0x106 || op == 0x107)
      isOn = phone_.mailReplyFlags.count(key) > 0;
    else if (op == 0x108 || op == 0x109)
      isOn = phone_.mailTriggerFlags.count(key) > 0;
    else
      isOn = phone_.mailReplySendFlags.count(key) > 0;
    t.stack.push_back(NsbValue::makeInt((isOn == wantOn) ? 1 : 0));
    break;
  }
  case 0x110: { // CheckUnReadMail
    (void)popArgs(t, args.size());
    int unread = 0;
    for (const auto &m : phone_.mailReceiveFlags) {
      if (phone_.mailViewFlags.count(m) == 0)
        ++unread;
    }
    t.stack.push_back(NsbValue::makeInt(unread));
    break;
  }
  case 0x116: { // GetMailBoxCursor
    (void)popArgs(t, args.size());
    t.stack.push_back(NsbValue::makeInt(phone_.mailBoxCursor));
    break;
  }
  case 0x114: { // DispAttachCg
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.attachedCg = normalizeAssetPath(a[0].asString());
    break;
  }
  case 0x115: { // EraseAttachCg
    (void)popArgs(t, args.size());
    phone_.attachedCg.clear();
    break;
  }
  case 0x119: { // WatchPhoneOpenExec
    (void)popArgs(t, args.size());
    phone_.phoneOpen = true;
    break;
  }
  case 0x11a: { // WatchPhoneEnd
    (void)popArgs(t, args.size());
    phone_.phoneOpen = false;
    break;
  }
  case 0x11d: { // PhoneWaitIcon
    auto a = popArgs(t, args.size());
    phone_.waitIconVisible = !a.empty() && a[0].isTruthy();
    break;
  }
  case 0x0ff: { // SetDic (Tips)
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.tipsUnlocked.insert(a[0].asString());
    break;
  }
  case 0x100: { // SetAchievement
    auto a = popArgs(t, args.size());
    if (!a.empty())
      phone_.achievementsUnlocked.insert(a[0].asString());
    break;
  }
  case 0x0e1: { // xbox360_GetKeyState
    (void)popArgs(t, args.size());
    t.stack.push_back(NsbValue::makeInt(0));
    break;
  }
  case 0x06b: { // GetScrollBarValue [name]
    auto a = popArgs(t, args.size());
    std::int64_t val = 0;
    if (!a.empty()) {
      if (const auto *e = sceneGraph_.findEntity(a[0].asString()))
        val = static_cast<std::int64_t>(std::lround(e->scrollValue));
    }
    t.stack.push_back(NsbValue::makeInt(val));
    break;
  }

  default: {
    // Gracefully consume any pushed arguments for unimplemented/cosmetic
    // strategy opcodes (SetShade, SetTone, SetBlur, SetShortcut, etc.).
    (void)popArgs(t, args.size());
    break;
  }
  }
}

SaveData NsbVm::saveState() const {
  SaveData data;
  data.chapterName = currentScriptPath();
  data.position = threads_.empty() ? 0 : threads_.front().pc;
  data.activeSpeaker = std::string(dialogueSystem_.speaker());
  data.activeText = std::string(dialogueSystem_.fullText());
  data.activeVoice = std::string(dialogueSystem_.voicePath());
  data.backlog = dialogueSystem_.backlog();
  data.runtimeBlob = writeRuntimeSnapshot();
  for (const auto &[k, v] : vars_) {
    if (v.kind == NsbValue::Kind::String)
      data.strings[k] = v.strVal;
    else
      data.integers[k] = v.asInt();
  }
  return data;
}

bool NsbVm::loadState(const SaveData &data) {
  if (!data.runtimeBlob.empty())
    return restoreRuntimeSnapshot(data.runtimeBlob);
  vars_.clear();
  for (const auto &[k, v] : data.integers)
    vars_[k] = NsbValue::makeInt(v);
  for (const auto &[k, v] : data.strings)
    vars_[k] = NsbValue::makeString(v);
  syncSystemVars();

  if (!data.chapterName.empty()) {
    jumpToScript(data.chapterName, "chapter.main");
    if (!threads_.empty() && threads_.front().script &&
        data.position < threads_.front().script->instructions().size()) {
      threads_.front().pc = data.position;
    }
  }
  dialogueSystem_.restore(data.activeSpeaker, data.activeText, data.activeVoice,
                          data.backlog, data.activeText.size(), 30.0f, false,
                          false);
  return true;
}

} // namespace N2System
