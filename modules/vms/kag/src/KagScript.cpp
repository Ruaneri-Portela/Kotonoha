#include <Kirikiri/KagScript.hpp>
#include <Kotonoha/core/Log.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
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
  std::string previousLabel;
  std::map<std::string, std::size_t> labelCounts;
  bool inInlineScript = false;
  std::size_t inlineScriptTag = 0;

  auto nextLine = [&]() {
    ++line;
    column = 1;
  };

  while (i < n) {
    // Inline TJS is opaque to KAG. A standalone closing tag ends the
    // block; brackets in TJS strings and array expressions are not tags.
    if (inInlineScript) {
      const auto lineStart = i;
      while (i < n && text[i] != '\r' && text[i] != '\n')
        ++i;
      const std::string rawLine = text.substr(lineStart, i - lineStart);
      std::string marker = trim(rawLine);
      std::transform(marker.begin(), marker.end(), marker.begin(),
                     [](unsigned char ch) { return std::tolower(ch); });
      if (marker == "[endscript]" || marker == "[endscript]\\" ||
          marker == "@endscript") {
        Tag closing;
        closing.type = TagType::Command;
        closing.name = "endscript";
        closing.line = line;
        const auto firstNonWhitespace = rawLine.find_first_not_of(" \t");
        closing.column = firstNonWhitespace == std::string::npos
                             ? 1
                             : static_cast<int>(firstNonWhitespace + 1);
        script.tags.push_back(std::move(closing));
        inInlineScript = false;
      } else {
        auto &body = script.tags.at(inlineScriptTag).text;
        if (body.size() + rawLine.size() + 1 > 256u * 1024u)
          throw std::runtime_error("KAG: inline TJS size limit exceeded");
        body += rawLine;
        body += '\n';
      }
      if (i < n) {
        if (text[i] == '\r' && i + 1 < n && text[i + 1] == '\n')
          ++i;
        ++i;
        nextLine();
      }
      continue;
    }

    char c = text[i];
    // KAG treats line-leading tabs as indentation, not dialogue. Also allow
    // spaces before a line-control marker to accommodate edited scenarios.
    bool lineControl = column == 1;
    if (lineControl && (c == ' ' || c == '\t')) {
      auto first = i;
      while (first < n && (text[first] == ' ' || text[first] == '\t'))
        ++first;
      if (first < n &&
          (text[first] == '*' || text[first] == ';' || text[first] == '@')) {
        column += static_cast<int>(first - i);
        i = first;
        c = text[i];
      } else {
        lineControl = false;
      }
    }

    if (c == '*' && lineControl) {
      // Bare KAG labels may be followed by a page title separated by '|'.
      // Repeated names receive a stable :2, :3 ... alias just as the
      // original KAG scenario label cache does.
      const int labelLine = line;
      const int labelColumn = column;
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
      const std::string base = trim(label);
      if (base.empty() && previousLabel.empty())
        throw std::runtime_error("KAG: unnamed first label in " + sourceName +
                                 " at line " + std::to_string(labelLine));
      const std::string resolved = base.empty() ? previousLabel : base;
      previousLabel = resolved;
      auto &occurrence = labelCounts[resolved];
      std::string alias = resolved;
      ++occurrence;
      if (occurrence > 1) {
        alias += ":" + std::to_string(occurrence);
        while (script.labels.count(alias))
          alias = resolved + ":" + std::to_string(++occurrence);
      }
      Tag tag;
      tag.type = TagType::Command;
      tag.name = "*" + alias;
      tag.line = labelLine;
      tag.column = labelColumn;
      script.labels.emplace(alias, script.tags.size());
      script.tags.push_back(std::move(tag));
    } else if (c == ';' && lineControl) {
      // KAG line comments must not become dialogue text.
      while (i < n && text[i] != '\r' && text[i] != '\n') {
        ++i;
        ++column;
      }
    } else if (c == '@' && lineControl) {
      // A command at the start of a line is equivalent to a bracketed tag.
      // Parse with the same attribute grammar to avoid diverging semantics.
      const int commandLineNumber = line;
      const int commandColumn = column;
      ++i;
      ++column;
      std::string commandLine;
      while (i < n && text[i] != '\r' && text[i] != '\n') {
        commandLine.push_back(text[i++]);
        ++column;
      }
      if (!trim(commandLine).empty()) {
        // Parse the @tag through the common attribute parser. An @iscript
        // opener needs a synthetic close in this *temporary* parse, otherwise
        // the nested call correctly reports a missing [endscript] too early.
        std::string commandName = trim(commandLine);
        const auto delimiter = commandName.find_first_of(" \t");
        if (delimiter != std::string::npos)
          commandName.resize(delimiter);
        std::transform(commandName.begin(), commandName.end(),
                       commandName.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        const bool startsInline = commandName == "iscript";
        auto parsed = parse("[" + commandLine + "]" +
                                (startsInline ? "\n[endscript]" : ""),
                            sourceName);
        if (startsInline)
          parsed.tags.pop_back(); // synthetic close; real close follows later
        for (const auto &entry : parsed.labels)
          script.labels.emplace(entry.first, script.tags.size() + entry.second);
        for (auto &tag : parsed.tags) {
          tag.line = commandLineNumber;
          tag.column = commandColumn;
          if (tag.name == "iscript") {
            inInlineScript = true;
            inlineScriptTag = script.tags.size();
          }
          script.tags.push_back(std::move(tag));
        }
      }
    } else if (c == '[' && !(i + 1 < n && text[i + 1] == '[')) {
      // Command tag
      const int tagStartLine = line;
      const int tagStartColumn = column;
      ++i;
      ++column;
      std::string tagName;
      while (i < n && text[i] != ']' && text[i] != ' ' && text[i] != '\t' &&
             text[i] != '\r' && text[i] != '\n') {
        tagName.push_back(text[i]);
        ++i;
        ++column;
      }
      // KAG command names are case-insensitive; labels are case-sensitive.
      if (!tagName.empty() && tagName.front() != '*')
        std::transform(tagName.begin(), tagName.end(), tagName.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
      Tag tag;
      tag.type = TagType::Command;
      tag.name = tagName;
      tag.line = tagStartLine;
      tag.column = tagStartColumn;

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
            if (i == n)
              throw std::runtime_error(
                  "KAG: unterminated quoted attribute in " + sourceName +
                  " at line " + std::to_string(tagStartLine));
            ++i;
            ++column; // skip closing quote
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
      if (i == n)
        throw std::runtime_error("KAG: unclosed tag in " + sourceName +
                                 " at line " + std::to_string(tagStartLine));
      ++i;
      ++column; // closing bracket
      // Register label if this tag is a label (name "l" or "*labelname")
      if (tag.name == "l" || tag.name == "*") {
        if (auto it = tag.attrs.find("name");
            it != tag.attrs.end() && !it->second.empty())
          script.labels.emplace(it->second, script.tags.size());
      } else if (!tag.name.empty() && tag.name[0] == '*') {
        script.labels.emplace(tag.name.substr(1), script.tags.size());
      }
      const bool startsInlineScript = tag.name == "iscript";
      script.tags.push_back(std::move(tag));
      if (startsInlineScript) {
        inInlineScript = true;
        inlineScriptTag = script.tags.size() - 1;
      }
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

  if (inInlineScript)
    throw std::runtime_error("KAG: unterminated [iscript] in " + sourceName +
                             " at line " + std::to_string(line));

  // Bounded static KAG macro expansion. Parse all definitions before
  // expanding commands so a game's common.ks macros can be referenced
  // later in this file without executing the macro body at declaration.
  // No native TJS2 eval is performed during this pass.
  using MacroBody = std::vector<Tag>;
  std::map<std::string, MacroBody> macros;
  std::vector<Tag> executable;
  for (std::size_t index = 0; index < script.tags.size(); ++index) {
    const auto &tag = script.tags[index];
    if (tag.type == TagType::Command && tag.name == "endmacro")
      throw std::runtime_error("KAG: unmatched [endmacro] in " + sourceName);
    if (tag.type != TagType::Command || tag.name != "macro") {
      executable.push_back(tag);
      continue;
    }
    const auto it = tag.attrs.find("name");
    if (it == tag.attrs.end() || it->second.empty())
      throw std::runtime_error("KAG: macro requires a name in " + sourceName);
    std::string macroName = it->second;
    std::transform(macroName.begin(), macroName.end(), macroName.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    if (macroName.size() > 64 || macros.count(macroName) != 0 ||
        macroName == "macro" || macroName == "endmacro")
      throw std::runtime_error("KAG: duplicate or invalid macro " + macroName);
    MacroBody body;
    bool closed = false;
    for (++index; index < script.tags.size(); ++index) {
      const auto &inside = script.tags[index];
      if (inside.type == TagType::Command && inside.name == "endmacro") {
        closed = true;
        break;
      }
      if (inside.type == TagType::Command &&
          (inside.name == "macro" ||
           (!inside.name.empty() && inside.name.front() == '*') ||
           (inside.name == "l" && inside.attrs.count("name"))))
        throw std::runtime_error(
            "KAG: labels/nested declarations not allowed in macro");
      if (body.size() >= 4096)
        throw std::runtime_error("KAG: macro definition too large");
      body.push_back(inside);
    }
    if (!closed)
      throw std::runtime_error("KAG: unterminated [macro] " + macroName);
    if (macros.size() >= 128)
      throw std::runtime_error("KAG: macro count limit exceeded");
    macros.emplace(std::move(macroName), std::move(body));
  }

  // Parameter references are whole attribute values of the form %name or
  // %name|fallback. A missing argument defaults to the empty string.
  const auto substitute = [](std::string value,
                             const std::map<std::string, std::string> &args) {
    if (value.size() <= 1 || value.front() != '%')
      return value;
    const auto bar = value.find('|');
    const auto key =
        value.substr(1, bar == std::string::npos ? std::string::npos : bar - 1);
    if (const auto it = args.find(key); it != args.end())
      return it->second;
    return bar == std::string::npos ? std::string() : value.substr(bar + 1);
  };
  std::vector<Tag> expanded;
  std::vector<std::string> activeMacros;
  std::function<void(Tag, const std::map<std::string, std::string> &)>
      appendExpanded;
  appendExpanded = [&](Tag tag, const auto &arguments) {
    for (auto &attr : tag.attrs)
      attr.second = substitute(std::move(attr.second), arguments);
    if (tag.type == TagType::Command) {
      if (const auto found = macros.find(tag.name); found != macros.end()) {
        if (activeMacros.size() >= 16 ||
            std::find(activeMacros.begin(), activeMacros.end(), tag.name) !=
                activeMacros.end())
          throw std::runtime_error("KAG: recursive macro " + tag.name);
        activeMacros.push_back(tag.name);
        for (const auto &item : found->second)
          appendExpanded(item, tag.attrs);
        activeMacros.pop_back();
        return;
      }
    }
    if (expanded.size() >= 65536)
      throw std::runtime_error("KAG: expanded tag limit exceeded");
    expanded.push_back(std::move(tag));
  };
  for (const auto &tag : executable)
    appendExpanded(tag, {});

  // Macro definition removal/expansion shifts indices. Rebuild labels
  // against executable tag positions to keep jump/call and saves consistent.
  script.tags = std::move(expanded);
  script.labels.clear();
  for (std::size_t index = 0; index < script.tags.size(); ++index) {
    const auto &tag = script.tags[index];
    if (tag.type != TagType::Command)
      continue;
    if (!tag.name.empty() && tag.name.front() == '*') {
      script.labels.emplace(tag.name.substr(1), index);
    } else if (tag.name == "l") {
      if (const auto found = tag.attrs.find("name");
          found != tag.attrs.end() && !found->second.empty())
        script.labels.emplace(found->second, index);
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
  pendingLinks_.clear();
  activeChoiceTargets_.clear();
  capturingLink_ = false;
  currentLinkCaption_.clear();
  currentLinkTarget_.clear();
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
  pendingLinks_.clear();
  activeChoiceTargets_.clear();
  capturingLink_ = false;
  currentLinkCaption_.clear();
  currentLinkTarget_.clear();
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
  pendingLinks_.clear();
  activeChoiceTargets_.clear();
  capturingLink_ = false;
  currentLinkCaption_.clear();
  currentLinkTarget_.clear();
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
  if (!activeChoiceTargets_.empty()) {
    if (index < 0 ||
        static_cast<std::size_t>(index) >= activeChoiceTargets_.size())
      throw std::out_of_range("KAG: selected menu index out of range");
    const std::string destination =
        activeChoiceTargets_[static_cast<std::size_t>(index)];
    activeChoiceTargets_.clear();
    if (!destination.empty()) {
      if (destination.front() == '*') {
        if (!jumpToLabel(destination))
          throw std::runtime_error("KAG: missing menu label " + destination);
        ifBranches_.clear();
      } else if (cb_.jump) {
        // Storage changes are deferred by KirikiriRuntime. Do not execute
        // the old scenario after scheduling its replacement.
        finished_ = true;
        cb_.jump(destination);
        return;
      }
    }
  }
  run();
}

void KagScript::Runner::resumeWait() {
  blocked_ = false;
  pendingResume_ = nullptr;
  run();
}

bool KagScript::Runner::executeTag(const Tag &tag) {
  if (tag.type == TagType::Text) {
    if (capturingLink_) {
      if (currentLinkCaption_.size() + tag.text.size() > 8192)
        throw std::runtime_error("KAG: [link] caption too long");
      currentLinkCaption_ += tag.text;
      return false;
    }
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
  // [ignore] is the inverse of [if]: a true expression excludes the
  // body through its matching [endignore], respecting nested blocks.
  if (name == "ignore") {
    if (!condition(tag))
      return false;
    std::size_t nested = 0;
    for (std::size_t i = pc_ + 1; i < script_.tags.size(); ++i) {
      const auto &candidate = script_.tags[i];
      if (candidate.type != TagType::Command)
        continue;
      if (candidate.name == "ignore") {
        ++nested;
      } else if (candidate.name == "endignore") {
        if (nested == 0) {
          pc_ = i; // run() advances past the closing tag.
          return false;
        }
        --nested;
      }
    }
    throw std::runtime_error("KAG: unterminated [ignore] block");
  }
  if (name == "endignore")
    return false;

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
  // A [glink] button or [link]...text...[endlink] contributes an
  // interactive option. [s] presents collected options through Kotonoha's
  // existing choice UI instead of silently terminating the scenario.
  const auto menuTarget = [&](const Tag &button) {
    const auto storage = getAttr(button, "storage", "");
    const auto label = getAttr(button, "target", "");
    return storage.empty() ? label
                           : storage + (label.empty() ? "" : "#" + label);
  };
  if (name == "glink" || name == "button") {
    // [button graphic=... target=*label] uses its graphic identifier as
    // a readable choice caption until native KAG button-layer rendering
    // is implemented. The navigation destination remains unchanged.
    if (pendingLinks_.size() >= 64)
      throw std::runtime_error("KAG: too many menu button options");
    auto caption = getAttr(tag, "text", getAttr(tag, "caption", ""));
    if (caption.empty())
      caption = getAttr(tag, "graphic", "Choice");
    pendingLinks_.push_back({std::move(caption), menuTarget(tag)});
    return false;
  }
  if (name == "link") {
    if (capturingLink_)
      throw std::runtime_error("KAG: nested [link] is not supported");
    capturingLink_ = true;
    currentLinkCaption_.clear();
    currentLinkTarget_ = menuTarget(tag);
    return false;
  }
  if (name == "endlink") {
    if (!capturingLink_)
      throw std::runtime_error("KAG: unexpected [endlink]");
    capturingLink_ = false;
    if (pendingLinks_.size() >= 64)
      throw std::runtime_error("KAG: too many link options");
    pendingLinks_.push_back({trim(currentLinkCaption_), currentLinkTarget_});
    currentLinkCaption_.clear();
    currentLinkTarget_.clear();
    return false;
  }
  if (name == "s" && !pendingLinks_.empty() && cb_.showChoice) {
    if (capturingLink_)
      throw std::runtime_error("KAG: unterminated [link] before [s]");
    std::vector<std::string> captions;
    activeChoiceTargets_.clear();
    for (const auto &option : pendingLinks_) {
      captions.push_back(option.caption);
      activeChoiceTargets_.push_back(option.target);
    }
    pendingLinks_.clear();
    blocked_ = true;
    cb_.showChoice("", std::move(captions),
                   [this](int index) { resumeChoice(index); });
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

  // Source was collected verbatim by the KAG lexer. The TJS2 VM runs it
  // without treating the inner brackets as KAG dialogue or commands.
  if (name == "iscript") {
    if (cb_.executeInlineTjs && !tag.text.empty())
      cb_.executeInlineTjs(tag.text);
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
  if (name == "layopt") {
    LayerOptions options;
    options.layer = getAttr(tag, "layer", "0");
    const auto number = [&](const char *key) -> std::optional<int> {
      const auto found = tag.attrs.find(key);
      if (found == tag.attrs.end())
        return std::nullopt;
      try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(found->second, &consumed);
        if (consumed != found->second.size())
          throw std::invalid_argument("trailing characters");
        return parsed;
      } catch (const std::exception &) {
        throw std::runtime_error(std::string("KAG: invalid layopt ") + key);
      }
    };
    options.left = number("left");
    options.top = number("top");
    options.opacity = number("opacity");
    options.order = number("order");
    if (const auto visible = tag.attrs.find("visible");
        visible != tag.attrs.end()) {
      std::string value = visible->second;
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char ch) { return std::tolower(ch); });
      if (value == "1" || value == "true" || value == "yes" || value == "on")
        options.visible = true;
      else if (value == "0" || value == "false" || value == "no" ||
               value == "off")
        options.visible = false;
      else
        throw std::runtime_error("KAG: invalid layopt visible");
    }
    if (cb_.updateLayer) {
      cb_.updateLayer(std::move(options));
    } else if (options.visible && !*options.visible) {
      // Legacy callback hosts do not retain layer state.
      if (options.layer == "base") {
        if (cb_.showBg)
          cb_.showBg("", 0);
      } else if (cb_.hideChar) {
        cb_.hideChar(options.layer, 0);
      }
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
  const auto expression = trim(getAttr(tag, "exp", getAttr(tag, "cond", "")));
  if (expression.empty())
    return false;

  // A bounded, side-effect-free subset of TJS2 predicates used by KAG
  // [if], [elsif] and [ignore]. Evaluate parentheses and &&/|| with the
  // correct precedence; searching for a comparator anywhere in a string
  // misinterprets quoted strings and compound title-menu conditions.
  const auto resolve = [this](std::string token) -> std::string {
    if (token == "true" || token == "false" || token == "null" ||
        token == "nil" || token.empty())
      return token;
    char *end = nullptr;
    (void)std::strtod(token.c_str(), &end);
    if (end != token.c_str() && end && *end == '\0')
      return token;
    if (token.front() == '&')
      token.erase(0, 1);
    if (const auto found = vars_.find(token); found != vars_.end())
      return found->second;
    return cb_.getVar ? cb_.getVar(token) : std::string();
  };

  struct Predicate {
    const std::string &input;
    std::function<std::string(std::string)> variable;
    std::size_t offset = 0;
    std::size_t terms = 0;
    std::size_t depth = 0;

    void spaces() {
      while (offset < input.size() &&
             std::isspace(static_cast<unsigned char>(input[offset])))
        ++offset;
    }
    bool match(const char *token) {
      spaces();
      const std::size_t size = std::char_traits<char>::length(token);
      if (input.compare(offset, size, token) != 0)
        return false;
      offset += size;
      return true;
    }
    [[noreturn]] void invalid() const {
      throw std::runtime_error(
          "KAG: unsupported conditional expression near offset " +
          std::to_string(offset));
    }
    static bool number(const std::string &value, double &out) {
      if (value.empty())
        return false;
      char *end = nullptr;
      out = std::strtod(value.c_str(), &end);
      return end != value.c_str() && end && *end == '\0' && std::isfinite(out);
    }
    static bool truthy(const std::string &value) {
      if (value.empty() || value == "false" || value == "nil" ||
          value == "null")
        return false;
      double parsed = 0;
      return !number(value, parsed) || parsed != 0.0;
    }
    std::string value() {
      spaces();
      if (++terms > 256 || offset >= input.size())
        invalid();
      if (input[offset] == '!') {
        ++offset;
        return truthy(value()) ? "0" : "1";
      }
      if (input[offset] == '(') {
        if (++depth > 32)
          invalid();
        ++offset;
        const bool result = disjunction();
        if (!match(")"))
          invalid();
        --depth;
        return result ? "1" : "0";
      }
      if (input[offset] == '\'' || input[offset] == '"') {
        const char quote = input[offset++];
        std::string value;
        bool closed = false;
        while (offset < input.size()) {
          char ch = input[offset++];
          if (ch == quote) {
            closed = true;
            break;
          }
          if (ch == '\\' && offset < input.size()) {
            ch = input[offset++];
            if (ch == 'n')
              ch = '\n';
            else if (ch == 't')
              ch = '\t';
          }
          value.push_back(ch);
        }
        if (!closed)
          invalid();
        return value;
      }
      const std::size_t start = offset;
      if (input[offset] == '&' &&
          (offset + 1 >= input.size() || input[offset + 1] != '&'))
        ++offset; // TJS/KAG dereference, e.g. &f.menuEnabled
      while (offset < input.size()) {
        const char ch = input[offset];
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == ')' ||
            ch == '(' || ch == '=' || ch == '!' || ch == '<' || ch == '>' ||
            ch == '&' || ch == '|')
          break;
        ++offset;
      }
      if (offset == start || (offset == start + 1 && input[start] == '&'))
        invalid();
      return variable(input.substr(start, offset - start));
    }
    bool comparison() {
      const auto left = value();
      const char *op = nullptr;
      for (const char *candidate : {"==", "!=", ">=", "<=", ">", "<"}) {
        if (match(candidate)) {
          op = candidate;
          break;
        }
      }
      if (!op)
        return truthy(left);
      const auto right = value();
      const std::string symbol = op;
      double leftNumber = 0, rightNumber = 0;
      const bool bothNumbers =
          number(left, leftNumber) && number(right, rightNumber);
      if (symbol == "==")
        return bothNumbers ? leftNumber == rightNumber : left == right;
      if (symbol == "!=")
        return bothNumbers ? leftNumber != rightNumber : left != right;
      if (!bothNumbers)
        return false;
      if (symbol == ">=")
        return leftNumber >= rightNumber;
      if (symbol == "<=")
        return leftNumber <= rightNumber;
      if (symbol == ">")
        return leftNumber > rightNumber;
      return leftNumber < rightNumber;
    }
    bool conjunction() {
      bool left = comparison();
      while (match("&&")) {
        const bool right = comparison();
        left = left && right;
      }
      return left;
    }
    bool disjunction() {
      bool left = conjunction();
      while (match("||")) {
        const bool right = conjunction();
        left = left || right;
      }
      return left;
    }
    bool evaluate() {
      if (input.size() > 4096)
        invalid();
      const bool result = disjunction();
      spaces();
      if (offset != input.size())
        invalid();
      return result;
    }
  };

  Predicate parser{expression, resolve};
  return parser.evaluate();
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
