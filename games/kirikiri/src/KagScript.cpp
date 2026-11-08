#include <Kirikiri/KagScript.hpp>
#include <Kotonoha/core/Log.hpp>

#include <algorithm>
#include <cctype>
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
    if (c == '[') {
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
      script.tags.push_back(std::move(tag));
      // Register label if this tag is a label (name "l" or "*labelname")
      if (tag.name == "l" || tag.name == "*") {
        if (auto it = tag.attrs.find("name");
            it != tag.attrs.end() && !it->second.empty())
          script.labels.emplace(it->second, script.tags.size() - 1);
      } else if (!tag.name.empty() && tag.name[0] == '*') {
        script.labels.emplace(tag.name.substr(1), script.tags.size() - 1);
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
      while (i < n && text[i] != '[') {
        if (text[i] == '\n') {
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

KagScript::Runner::Runner(Script script, const Callbacks &cb)
    : script_(std::move(script)), cb_(cb) {}

void KagScript::Runner::run() {
  SDL_Log("KagScript: run() start pc_=%zu size=%zu blocked=%d", pc_,
          script_.tags.size(), blocked_ ? 1 : 0);
  while (pc_ < script_.tags.size() && !blocked_) {
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
    return true; // text blocks until player advances
  }

  const std::string &name = tag.name;

  // Built-in navigation
  if (name == "jump" || name == "j") {
    SDL_Log("KagScript: tag jump");
    if (cb_.jump) {
      auto target = getAttr(tag, "storage", std::string());
      auto label = getAttr(tag, "target", std::string());
      if (!label.empty())
        cb_.jump(label);
      else if (!target.empty())
        cb_.jump(target);
    }
    return false;
  }
  if (name == "call") {
    SDL_Log("KagScript: tag call");
    if (cb_.jump) {
      auto target = getAttr(tag, "storage", std::string());
      if (!target.empty())
        cb_.jump(target);
    }
    return false;
  }
  if (name == "return") {
    SDL_Log("KagScript: tag return");
    finished_ = true;
    return true;
  }
  if (name == "end") {
    SDL_Log("KagScript: tag end");
    finished_ = true;
    return true;
  }

  // Label tags are no-ops at runtime.
  if (name == "l" || name == "*" || (!name.empty() && name[0] == '*')) {
    SDL_Log("KagScript: tag label %s", name.c_str());
    return false;
  }

  // Text display
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
    std::vector<std::string> options;
    for (auto &[k, v] : tag.attrs) {
      if (k.rfind("option", 0) == 0 || k.rfind("btn", 0) == 0)
        options.push_back(v);
    }
    if (cb_.showChoice && !options.empty()) {
      blocked_ = true;
      cb_.showChoice(getAttr(tag, "question", std::string()), options,
                     [this](int index) { resumeChoice(index); });
    }
    return true; // choice blocks until player chooses
  }

  // Wait
  if (name == "wait" || name == "w") {
    int ms = getAttrInt(tag, "time", 0);
    if (ms > 0 && cb_.wait) {
      blocked_ = true;
      cb_.wait(ms, [this] { resumeWait(); });
    }
    return true; // wait blocks until timer expires
  }

  // Background
  if (name == "bgshow" || name == "bg" || name == "back") {
    if (cb_.showBg)
      cb_.showBg(getAttr(tag, "file", std::string()),
                 getAttrInt(tag, "transition", 0));
    return false;
  }

  // Character
  if (name == "chara_show" || name == "cshow" || name == "show") {
    if (cb_.showChar)
      cb_.showChar(getAttr(tag, "layer", "0"),
                   getAttr(tag, "file", std::string()), getAttrInt(tag, "x", 0),
                   getAttrInt(tag, "y", 0), getAttrInt(tag, "transition", 0));
    return false;
  }
  if (name == "chara_hide" || name == "chide" || name == "hide") {
    if (cb_.hideChar)
      cb_.hideChar(getAttr(tag, "layer", "0"),
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

  // Variables
  if (name == "set" || name == "eval") {
    if (cb_.setVar) {
      auto name_ = getAttr(tag, "name", std::string());
      auto value = getAttr(tag, "value", std::string());
      if (!name_.empty())
        cb_.setVar(name_, value);
    }
    return false;
  }

  // Unknown tag: log and skip.
  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
              "Kirikiri: unknown tag '%s' at %s:%d:%d", name.c_str(), tag.line,
              tag.column);
  return false;
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
