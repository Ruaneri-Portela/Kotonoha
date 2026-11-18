#include <Kirikiri/KagScript.hpp>
#include <Kotonoha/core/Log.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace Kirikiri {

namespace {

bool isNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
         c == '/';
}

std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

} // namespace

KagScript::Script KagScript::parse(const std::string &text,
                                   const std::string &sourceName) {
  Script script;
  int line = 1, column = 1;
  std::size_t i = 0, n = text.size();

  auto nextLine = [&]() {
    ++line;
    column = 1;
  };

  while (i < n) {
    char c = text[i];
    if (c == '*' && column == 1) {
      // KAG labels are normally bare lines (`*label|caption`), not bracketed
      // command tags.  Keep the label as a no-op tag so saved PCs stay stable.
      const auto labelLine = line;
      ++i;
      ++column;
      std::string label;
      while (i < n && text[i] != '|' && text[i] != '\r' && text[i] != '\n') {
        label.push_back(text[i++]);
        ++column;
      }
      while (i < n && text[i] != '\r' && text[i] != '\n') {
        ++i;
        ++column;
      }
      Tag tag;
      tag.type = TagType::Command;
      tag.name = "*" + label;
      tag.line = labelLine;
      tag.column = 1;
      script.labels.emplace(label, script.tags.size());
      script.tags.push_back(std::move(tag));
    } else if (c == ';' && column == 1) {
      // KAG line comments must not become dialogue text.
      while (i < n && text[i] != '\r' && text[i] != '\n') {
        ++i;
        ++column;
      }
    } else if (c == '@' && column == 1) {
      // A command at the start of a line is equivalent to a bracketed tag.
      // Parse with the same attribute grammar to avoid diverging semantics.
      const int commandLineNumber = line;
      ++i;
      ++column;
      std::string commandLine;
      while (i < n && text[i] != '\r' && text[i] != '\n') {
        commandLine.push_back(text[i++]);
        ++column;
      }
      if (!trim(commandLine).empty()) {
        auto parsed = parse("[" + commandLine + "]", sourceName);
        for (const auto &entry : parsed.labels)
          script.labels.emplace(entry.first, script.tags.size() + entry.second);
        for (auto &tag : parsed.tags) {
          tag.line = commandLineNumber;
          tag.column = 1;
          script.tags.push_back(std::move(tag));
        }
      }
    } else if (c == '[' && !(i + 1 < n && text[i + 1] == '[')) {
      // Command tag
      ++i;
      ++column;
      std::string tagName;
      while (i < n && text[i] != ']' && text[i] != ' ' && text[i] != '\t' &&
             text[i] != '\r' && text[i] != '\n') {
        tagName.push_back(text[i]);
        ++i;
        ++column;
      }
      Tag tag;
      tag.type = TagType::Command;
      tag.name = tagName;
      tag.line = line;
      tag.column = column;

      // Parse attributes: name="value" or name=value
      while (i < n && text[i] != ']') {
        // skip whitespace
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' ||
                         text[i] == '\n')) {
          if (text[i] == '\n')
            nextLine();
          else
            ++column;
          ++i;
        }
        if (i >= n || text[i] == ']')
          break;
        // attribute name
        std::string attrName;
        while (i < n && text[i] != '=' && text[i] != ' ' && text[i] != '\t' &&
               text[i] != '\r' && text[i] != '\n' && text[i] != ']') {
          attrName.push_back(text[i]);
          ++i;
          ++column;
        }
        std::string attrValue;
        if (i < n && text[i] == '=') {
          ++i;
          ++column;
          if (i < n && (text[i] == '"' || text[i] == '\'')) {
            char quote = text[i];
            ++i;
            ++column;
            while (i < n && text[i] != quote) {
              attrValue.push_back(text[i]);
              ++i;
              ++column;
            }
            if (i < n) {
              ++i;
              ++column; // skip closing quote
            }
          } else {
            while (i < n && text[i] != ' ' && text[i] != '\t' &&
                   text[i] != '\r' && text[i] != '\n' && text[i] != ']') {
              attrValue.push_back(text[i]);
              ++i;
              ++column;
            }
          }
        }
        if (!attrName.empty())
          tag.attrs.emplace(std::move(attrName), std::move(attrValue));
      }
      if (i < n && text[i] == ']') {
        ++i;
        ++column;
      }
      // Register label if this tag is a label (name "l" or "*labelname")
      if (tag.name == "l" || tag.name == "*") {
        if (auto it = tag.attrs.find("name");
            it != tag.attrs.end() && !it->second.empty())
          script.labels.emplace(it->second, script.tags.size());
      } else if (!tag.name.empty() && tag.name[0] == '*') {
        script.labels.emplace(tag.name.substr(1), script.tags.size());
      }
      script.tags.push_back(std::move(tag));
    } else if (c == '\n') {
      nextLine();
      ++i;
    } else if (c == '\r') {
      ++i;
      if (i < n && text[i] == '\n')
        ++i;
      nextLine();
    } else {
      // Plain text accumulates until next '['
      std::string textBuf;
      while (i < n && (text[i] != '[' || (i + 1 < n && text[i + 1] == '['))) {
        if (text[i] == '[') {
          // KAG [[ escapes a literal [ in dialogue.
          textBuf.push_back('[');
          i += 2;
          column += 2;
        } else if (text[i] == '\n') {
          nextLine();
          ++i;
          break;
        } else if (text[i] == '\r') {
          ++i;
          if (i < n && text[i] == '\n')
            ++i;
          nextLine();
          break;
        } else {
          textBuf.push_back(text[i]);
          ++i;
          ++column;
        }
      }
      if (!textBuf.empty()) {
        Tag t;
        t.type = TagType::Text;
        t.text = std::move(textBuf);
        t.line = line;
        t.column = column;
        script.tags.push_back(std::move(t));
      }
    }
  }

  return script;
}

KagScript::Runner::Runner(Script script, Callbacks cb)
    : script_(std::move(script)), cb_(std::move(cb)) {}

KagScript::Runner::State KagScript::Runner::saveState() const {
  return {pc_, returnStack_, finished_, vars_, ifBranches_, callIfDepth_};
}

void KagScript::Runner::restoreState(const State &state) {
  if (state.position > script_.tags.size())
    throw std::out_of_range("KAG save position exceeds scenario length");
  for (const auto frame : state.returnStack)
    if (frame == 0 || frame > script_.tags.size())
      throw std::out_of_range(
          "KAG save return address exceeds scenario length");
  if (state.ifBranches.size() > script_.tags.size() ||
      state.callIfDepth.size() != state.returnStack.size())
    throw std::out_of_range("KAG save conditional stack is inconsistent");
  for (auto branch : state.ifBranches)
    if (branch > 1)
      throw std::out_of_range("KAG invalid branch state");
  for (const auto depth : state.callIfDepth)
    if (depth > state.ifBranches.size())
      throw std::out_of_range("KAG save call branch depth is invalid");
  // Every check above is complete before the VM state is changed.
  pc_ = state.position;
  returnStack_ = state.returnStack;
  vars_ = state.variables;
  ifBranches_ = state.ifBranches;
  callIfDepth_ = state.callIfDepth;
  finished_ = state.finished || pc_ == script_.tags.size();
  blocked_ = false;
  pendingResume_ = nullptr;
}

void KagScript::Runner::restorePosition(std::size_t position) {
  if (position > script_.tags.size())
    throw std::out_of_range("KAG save position exceeds scenario length");
  pc_ = position;
  blocked_ = false;
  finished_ = pc_ == script_.tags.size();
  pendingResume_ = nullptr;
  // A plain PC restore cannot recover an active KAG subroutine stack.
  returnStack_.clear();
  ifBranches_.clear();
  callIfDepth_.clear();
}

bool KagScript::Runner::jumpToLabel(const std::string &label) {
  // Real KAG scripts commonly refer to labels as target=*label.
  const auto normalized =
      !label.empty() && label.front() == '*' ? label.substr(1) : label;
  auto position = findLabel(normalized);
  if (position == script_.tags.size())
    return false;
  // A jump inside a subroutine must keep its return frames. Only an explicit
  // restorePosition() resets the call stack.
  pc_ = position;
  blocked_ = finished_ = false;
  pendingResume_ = nullptr;
  return true;
}

void KagScript::Runner::run() {
  SDL_Log("KagScript: run() start pc_=%zu size=%zu blocked=%d", pc_,
          script_.tags.size(), blocked_ ? 1 : 0);
  // Yield back to the host if a script loops without waiting for input.
  std::size_t budget = 8192;
  while (pc_ < script_.tags.size() && !blocked_ && !finished_ && budget-- > 0) {
    SDL_Log("KagScript: run() executing tag pc_=%zu name='%s'", pc_,
            script_.tags[pc_].name.c_str());
    bool blocks = executeTag(script_.tags[pc_]);
    ++pc_;
    if (blocks) {
      blocked_ = true;
      SDL_Log("KagScript: run() blocked at pc_=%zu", pc_);
      break;
    }
  }
  if (pc_ >= script_.tags.size()) {
    SDL_Log("KagScript: run() finished");
    finished_ = true;
  }
}

void KagScript::Runner::resumeChoice(int index) {
  blocked_ = false;
  pendingResume_ = nullptr;
  run();
}

void KagScript::Runner::resumeWait() {
  blocked_ = false;
  pendingResume_ = nullptr;
  run();
}

bool KagScript::Runner::executeTag(const Tag &tag) {
  if (tag.type == TagType::Text) {
    std::string text = trim(tag.text);
    if (!text.empty() && cb_.showText) {
      SDL_Log("KagScript: Text tag '%s'", text.c_str());
      cb_.showText(std::string(), text);
    }
    return !text.empty() && static_cast<bool>(cb_.showText);
    // Whitespace-only indentation is not a dialogue action.
  }

  const std::string &name = tag.name;

  // KAG's if/elsif/else/endif select one branch per nesting level.
  // Rather than reevaluating an earlier branch, retain a matched flag for
  // each open [if]. This is also persisted in version-4 save states.
  auto nextBranch = [&](bool includeAlternatives) {
    unsigned nested = 0;
    for (std::size_t i = pc_ + 1; i < script_.tags.size(); ++i) {
      const auto &candidate = script_.tags[i];
      if (candidate.type != TagType::Command)
        continue;
      if (candidate.name == "if") {
        ++nested;
      } else if (candidate.name == "endif") {
        if (nested == 0)
          return i;
        --nested;
      } else if (nested == 0 && includeAlternatives &&
                 (candidate.name == "elsif" || candidate.name == "else")) {
        return i;
      }
    }
    throw std::runtime_error("KAG: unterminated [if] block");
  };
  if (name == "if") {
    if (ifBranches_.size() >= 1024)
      throw std::runtime_error("KAG: conditional nesting limit exceeded");
    const bool matched = condition(tag);
    ifBranches_.push_back(matched ? 1u : 0u);
    if (!matched)
      pc_ = nextBranch(true) - 1; // execute candidate on next loop
    return false;
  }
  if (name == "elsif" || name == "else") {
    if (ifBranches_.empty())
      throw std::runtime_error("KAG: orphan else/elsif");
    if (ifBranches_.back() != 0) {
      pc_ = nextBranch(false); // matched earlier; skip the rest and endif
      ifBranches_.pop_back();
    } else if (name == "else") {
      ifBranches_.back() = 1;
    } else if (condition(tag)) {
      ifBranches_.back() = 1;
    } else {
      pc_ = nextBranch(true) - 1;
    }
    return false;
  }
  if (name == "endif") {
    if (ifBranches_.empty())
      throw std::runtime_error("KAG: orphan endif");
    ifBranches_.pop_back();
    return false;
  }

  // Built-in navigation
  if (name == "jump" || name == "j") {
    SDL_Log("KagScript: tag jump");
    const auto target = getAttr(tag, "storage", std::string());
    const auto label = getAttr(tag, "target", std::string());
    if (!label.empty() && target.empty()) {
      if (!jumpToLabel(label))
        throw std::runtime_error("KAG: missing label " + label);
      ifBranches_.clear(); // leaving conditional control flow
    } else if (cb_.jump && !target.empty()) {
      cb_.jump(label.empty() ? target : target + "#" + label);
    }
    return false;
  }
  if (name == "call") {
    SDL_Log("KagScript: tag call");
    const auto storage = getAttr(tag, "storage", std::string());
    const auto label = getAttr(tag, "target", std::string());
    if (storage.empty() && !label.empty()) {
      // The run loop advances the PC after executing this tag, so store the
      // next instruction and jump to the label (which is a no-op tag).
      const auto resumeAt = pc_ + 1;
      if (returnStack_.size() >= 1024)
        throw std::runtime_error("KAG: call stack limit exceeded");
      const auto ifDepth = ifBranches_.size();
      if (jumpToLabel(label)) {
        returnStack_.push_back(resumeAt);
        callIfDepth_.push_back(ifDepth);
      } else
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Kirikiri: unknown local call target '%s'", label.c_str());
    } else if (cb_.jump && !storage.empty()) {
      cb_.jump(label.empty() ? storage : storage + "#" + label);
    }
    return false;
  }
  if (name == "return") {
    SDL_Log("KagScript: tag return");
    if (!returnStack_.empty()) {
      // run() unconditionally increments pc_ after executeTag().
      const auto resumeAt = returnStack_.back();
      const auto depth = callIfDepth_.back();
      if (ifBranches_.size() < depth)
        throw std::runtime_error("KAG: subroutine ended caller conditional");
      returnStack_.pop_back();
      callIfDepth_.pop_back();
      ifBranches_.resize(depth);
      pc_ = resumeAt - 1;
      return false;
    }
    finished_ = true;
    return true;
  }
  if (name == "end" || name == "s") {
    SDL_Log("KagScript: tag %s", name.c_str());
    finished_ = true;
    return true;
  }

  // Label tags or no-op markers.
  if (name == "*" || (!name.empty() && name[0] == '*')) {
    SDL_Log("KagScript: tag label %s", name.c_str());
    return false;
  }

  // Inline TJS2 block in KAG scripts. Skip until matching [endscript].
  if (name == "iscript") {
    for (std::size_t i = pc_ + 1; i < script_.tags.size(); ++i) {
      if (script_.tags[i].type == TagType::Command &&
          script_.tags[i].name == "endscript") {
        pc_ = i;
        break;
      }
    }
    return false;
  }
  if (name == "endscript")
    return false;

  // Text display and click waits
  if (name == "l") {
    if (tag.attrs.count("name") && !tag.attrs.at("name").empty())
      return false; // label tag
    SDL_Log("KagScript: tag l (line click wait)");
    return true; // yield for click
  }
  if (name == "p") {
    SDL_Log("KagScript: tag p (page click wait)");
    return true; // yield for click
  }
  if (name == "r" || name == "position" || name == "current" ||
      name == "quake" || name == "shake" || name == "locksnapshot" ||
      name == "unlocksnapshot" || name == "history" || name == "dialog" ||
      name == "stoptrans" || name == "syspage" || name == "systrans" ||
      name == "rclick" || name == "sysrestore" || name == "msgoff" ||
      name == "meswinload" || name == "touchmode" || name == "beginskip" ||
      name == "endskip")
    return false; // KAG layout, system-menu and KAGEnv state tags

  if (name == "cm") {
    SDL_Log("KagScript: tag cm");
    if (cb_.showText) {
      auto text = getAttr(tag, "text", std::string());
      auto speaker = getAttr(tag, "speaker", std::string());
      if (!text.empty())
        cb_.showText(speaker, text);
    }
    return true; // cm blocks until player advances
  }

  // Choices
  if (name == "select" || name == "choices") {
    // Attributes are held in a std::map; lexicographic iteration presents
    // option10 before option2. Sort numbered buttons numerically, and never
    // treat unrelated attributes such as optionColor as dialogue choices.
    struct Choice {
      unsigned index;
      std::string key, caption;
    };
    std::vector<Choice> numbered;
    for (const auto &[key, caption] : tag.attrs) {
      const std::size_t prefix = key.rfind("option", 0) == 0 ? 6u
                                 : key.rfind("btn", 0) == 0  ? 3u
                                                             : 0u;
      if (!prefix || key.size() <= prefix)
        continue;
      unsigned index = 0;
      bool valid = true;
      for (std::size_t i = prefix; i < key.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(key[i]);
        if (ch < '0' || ch > '9' || index > 9999u) {
          valid = false;
          break;
        }
        index = index * 10u + (ch - '0');
      }
      if (valid && index <= 9999u)
        numbered.push_back({index, key, caption});
    }
    std::sort(numbered.begin(), numbered.end(),
              [](const Choice &a, const Choice &b) {
                return a.index == b.index ? a.key < b.key : a.index < b.index;
              });
    std::vector<std::string> options;
    options.reserve(numbered.size());
    for (const auto &choice : numbered)
      options.push_back(choice.caption);
    if (cb_.showChoice && !options.empty()) {
      blocked_ = true;
      cb_.showChoice(getAttr(tag, "question", std::string()), options,
                     [this](int index) { resumeChoice(index); });
      return true;
    }
    return false; // no choice callback; do not deadlock the VM
  }

  // Wait
  if (name == "wait" || name == "w") {
    int ms = getAttrInt(tag, "time", 0);
    if (ms > 0 && cb_.wait) {
      blocked_ = true;
      cb_.wait(ms, [this] { resumeWait(); });
      return true;
    }
    return false; // a missing timer callback must not suspend forever
  }

  // KAG3's regular [bg] command uses storage= rather than the custom
  // bgshow file= alias. Both are supported for older Kotonoha scripts.
  if (name == "bgshow" || name == "bg" || name == "back") {
    if (cb_.showBg)
      cb_.showBg(getAttr(tag, "storage", getAttr(tag, "file", "")),
                 getAttrInt(tag, "time", getAttrInt(tag, "transition", 0)));
    return false;
  }

  // Native KAG [image layer=base storage=...] and foreground layers.
  // This initial renderer presents the front page directly; synchronized
  // back/fore page swaps and KAG transitions require a separate VM feature.
  if (name == "image") {
    const auto file = getAttr(tag, "storage", getAttr(tag, "file", ""));
    const auto layer = getAttr(tag, "layer", "base");
    if (layer == "base") {
      if (cb_.showBg)
        cb_.showBg(file, 0);
    } else if (cb_.showChar) {
      cb_.showChar(layer, file,
                   getAttrInt(tag, "left", getAttrInt(tag, "x", 0)),
                   getAttrInt(tag, "top", getAttrInt(tag, "y", 0)), 0);
    }
    return false;
  }
  if (name == "freeimage" || name == "chara_hide" || name == "chide" ||
      name == "hide") {
    const auto layer = getAttr(tag, "layer", "0");
    if (layer == "base") {
      if (cb_.showBg)
        cb_.showBg("", 0);
    } else if (cb_.hideChar) {
      cb_.hideChar(layer, getAttrInt(tag, "transition", 0));
    }
    return false;
  }
  if (name == "layopt" && !getAttrBool(tag, "visible", true)) {
    const auto layer = getAttr(tag, "layer", "0");
    if (layer == "base") {
      if (cb_.showBg)
        cb_.showBg("", 0);
    } else if (cb_.hideChar) {
      cb_.hideChar(layer, 0);
    }
    return false;
  }
  if (name == "trans" || name == "wt" || name == "backlay" || name == "forelay")
    return false; // Immediate presentation, no authentic timing yet.

  // Kotonoha's explicit character commands.
  if (name == "chara_show" || name == "cshow" || name == "show" ||
      name == "chara_mod" || name == "cmod" || name == "chara_pose") {
    if (cb_.showChar)
      cb_.showChar(getAttr(tag, "layer", "0"),
                   getAttr(tag, "storage", getAttr(tag, "file", "")),
                   getAttrInt(tag, "left", getAttrInt(tag, "x", 0)),
                   getAttrInt(tag, "top", getAttrInt(tag, "y", 0)),
                   getAttrInt(tag, "transition", 0));
    return false;
  }

  // Audio
  if (name == "playbgm" || name == "bgm") {
    if (cb_.playBgm)
      cb_.playBgm(getAttr(tag, "file", std::string()),
                  getAttrBool(tag, "loop", false));
    return false;
  }
  if (name == "stopbgm") {
    if (cb_.stopBgm)
      cb_.stopBgm();
    return false;
  }
  if (name == "playse" || name == "se") {
    if (cb_.playSe)
      cb_.playSe(getAttr(tag, "file", std::string()));
    return false;
  }

  // A small KAG variable subset, separate from the full TJS2 evaluator.
  if (name == "set" || name == "eval") {
    const auto key = getAttr(tag, "name", std::string());
    const auto value = getAttr(tag, "value", std::string());
    if (!key.empty()) {
      vars_[key] = value;
      if (cb_.setVar)
        cb_.setVar(key, value);
    }
    return false;
  }

  // Unknown tag: log and skip.
  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
              "Kirikiri: unknown tag '%s' at %d:%d", name.c_str(), tag.line,
              tag.column);
  return false;
}

bool KagScript::Runner::condition(const Tag &tag) const {
  const auto expr = trim(getAttr(tag, "exp", getAttr(tag, "cond", "")));
  if (expr.empty())
    return false;
  const auto operand = [this](std::string token) -> std::string {
    token = trim(token);
    if (token.size() > 1 && ((token.front() == '"' && token.back() == '"') ||
                             (token.front() == '\'' && token.back() == '\'')))
      return token.substr(1, token.size() - 2);
    if (token == "true" || token == "false" || token == "null" ||
        token == "nil" || token.empty())
      return token;
    char *end = nullptr;
    (void)std::strtod(token.c_str(), &end);
    if (end != token.c_str() && *end == 0)
      return token;
    if (!token.empty() && token.front() == '&')
      token.erase(0, 1);
    const auto found = vars_.find(token);
    if (found != vars_.end())
      return found->second;
    return cb_.getVar ? cb_.getVar(token) : std::string();
  };
  const auto truthy = [](const std::string &value) {
    if (value.empty() || value == "false" || value == "nil" || value == "null")
      return false;
    char *end = nullptr;
    const auto number = std::strtod(value.c_str(), &end);
    if (end != value.c_str() && *end == 0)
      return number != 0.0;
    return true;
  };
  for (const char *op : {"==", "!=", ">=", "<=", ">", "<"}) {
    const auto pos = expr.find(op);
    if (pos == std::string::npos)
      continue;
    const auto left = operand(expr.substr(0, pos));
    const auto right =
        operand(expr.substr(pos + std::char_traits<char>::length(op)));
    if (std::string(op) == "==")
      return left == right;
    if (std::string(op) == "!=")
      return left != right;
    char *endL = nullptr, *endR = nullptr;
    const auto lv = std::strtod(left.c_str(), &endL);
    const auto rv = std::strtod(right.c_str(), &endR);
    if (endL == left.c_str() || *endL || endR == right.c_str() || *endR)
      return false;
    if (std::string(op) == ">=")
      return lv >= rv;
    if (std::string(op) == "<=")
      return lv <= rv;
    if (std::string(op) == ">")
      return lv > rv;
    return lv < rv;
  }
  if (expr.front() == '!')
    return !truthy(operand(expr.substr(1)));
  return truthy(operand(expr));
}

std::size_t KagScript::Runner::findLabel(const std::string &name) {
  auto it = script_.labels.find(name);
  return it == script_.labels.end() ? script_.tags.size() : it->second;
}

bool KagScript::Runner::getAttrBool(const Tag &tag, const std::string &key,
                                    bool def) const {
  auto it = tag.attrs.find(key);
  if (it == tag.attrs.end())
    return def;
  const std::string &v = it->second;
  return v == "1" || v == "true" || v == "yes" || v == "on";
}

int KagScript::Runner::getAttrInt(const Tag &tag, const std::string &key,
                                  int def) const {
  auto it = tag.attrs.find(key);
  if (it == tag.attrs.end())
    return def;
  try {
    return std::stoi(it->second);
  } catch (...) {
    return def;
  }
}

std::string KagScript::Runner::getAttr(const Tag &tag, const std::string &key,
                                       const std::string &def) const {
  auto it = tag.attrs.find(key);
  return it == tag.attrs.end() ? def : it->second;
}

} // namespace Kirikiri
