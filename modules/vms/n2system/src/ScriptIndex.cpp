// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ScriptIndex.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace N2System {
namespace {
enum class Kind { End, Identifier, String, Symbol };
struct Token {
  Kind kind = Kind::End;
  std::string text;
  std::size_t offset = 0;
  std::size_t end = 0;
  unsigned line = 1;
};
bool wordStart(unsigned char c) {
  return std::isalpha(c) != 0 || c == '_' || c >= 0x80;
}
bool wordNext(unsigned char c) { return wordStart(c) || std::isdigit(c) != 0; }
class Scanner {
public:
  explicit Scanner(std::string_view source) : source_(source) {
    if (source_.size() >= 3 && static_cast<unsigned char>(source_[0]) == 0xef &&
        static_cast<unsigned char>(source_[1]) == 0xbb &&
        static_cast<unsigned char>(source_[2]) == 0xbf)
      pos_ = 3;
  }
  Token next() {
    trivia();
    Token t;
    t.offset = pos_;
    t.line = line_;
    if (pos_ == source_.size()) {
      t.end = pos_;
      return t;
    }
    const auto c = static_cast<unsigned char>(source_[pos_]);
    if (wordStart(c)) {
      t.kind = Kind::Identifier;
      do {
        ++pos_;
      } while (pos_ < source_.size() &&
               wordNext(static_cast<unsigned char>(source_[pos_])));
    } else if (c == '"' || c == '\'') {
      t.kind = Kind::String;
      const char quote = source_[pos_++];
      bool closed = false;
      while (pos_ < source_.size()) {
        const char value = source_[pos_++];
        if (value == '\\' && pos_ < source_.size()) {
          if (source_[pos_] == '\r') {
            ++line_;
            ++pos_;
            if (pos_ < source_.size() && source_[pos_] == '\n')
              ++pos_;
          } else {
            if (source_[pos_] == '\n')
              ++line_;
            ++pos_;
          }
        } else if (value == quote) {
          closed = true;
          break;
        } else if (value == '\r') {
          ++line_;
          if (pos_ < source_.size() && source_[pos_] == '\n')
            ++pos_;
        } else if (value == '\n') {
          ++line_;
        }
      }
      if (!closed)
        throw std::runtime_error("N2System: unterminated string at line " +
                                 std::to_string(t.line));
    } else {
      t.kind = Kind::Symbol;
      if (source_[pos_++] == '\n')
        ++line_;
    }
    t.end = pos_;
    t.text = std::string(source_.substr(t.offset, t.end - t.offset));
    return t;
  }

private:
  void trivia() {
    while (pos_ < source_.size()) {
      const char c = source_[pos_];
      if (std::isspace(static_cast<unsigned char>(c))) {
        if (c == '\r') {
          ++line_;
          ++pos_;
          if (pos_ < source_.size() && source_[pos_] == '\n')
            ++pos_;
        } else {
          if (c == '\n')
            ++line_;
          ++pos_;
        }
      } else if (c == '/' && pos_ + 1 < source_.size() &&
                 source_[pos_ + 1] == '/') {
        pos_ += 2;
        while (pos_ < source_.size() && source_[pos_] != '\n' &&
               source_[pos_] != '\r')
          ++pos_;
      } else if (c == '/' && pos_ + 1 < source_.size() &&
                 source_[pos_ + 1] == '*') {
        const unsigned startLine = line_;
        pos_ += 2;
        bool closed = false;
        while (pos_ + 1 < source_.size()) {
          if (source_[pos_] == '\n' ||
              (source_[pos_] == '\r' && source_[pos_ + 1] != '\n'))
            ++line_;
          if (source_[pos_] == '*' && source_[pos_ + 1] == '/') {
            pos_ += 2;
            closed = true;
            break;
          }
          ++pos_;
        }
        if (!closed)
          throw std::runtime_error("N2System: unterminated comment at line " +
                                   std::to_string(startLine));
      } else {
        return;
      }
    }
  }
  std::string_view source_;
  std::size_t pos_ = 0;
  unsigned line_ = 1;
};
bool startsLine(std::string_view source, std::size_t offset) {
  const auto newline = offset == 0 ? std::string_view::npos
                                   : source.find_last_of("\r\n", offset - 1);
  const auto first = newline == std::string_view::npos ? 0 : newline + 1;
  for (std::size_t i = first; i < offset; ++i)
    if (source[i] != ' ' && source[i] != '\t' && source[i] != '\r')
      return false;
  return true;
}
} // namespace

const Label *Chapter::findLabel(std::string_view name) const {
  for (const auto &label : labels)
    if (label.name == name)
      return &label;
  return nullptr;
}
const Chapter *ScriptIndex::findChapter(std::string_view name) const {
  for (const auto &chapter : chapters_)
    if (chapter.name == name)
      return &chapter;
  return nullptr;
}
ScriptIndex ScriptIndex::Parse(std::string_view source) {
  ScriptIndex result;
  Scanner scanner(source);
  bool active = false;
  int depth = 0;
  Token previous;
  while (true) {
    const Token t = scanner.next();
    if (t.kind == Kind::End)
      break;
    if (!active) {
      if (t.kind != Kind::Identifier || t.text != "chapter")
        continue;
      const Token name = scanner.next();
      if (name.kind != Kind::Identifier)
        throw std::runtime_error("N2System: expected chapter name at line " +
                                 std::to_string(t.line));
      const Token brace = scanner.next();
      if (brace.text != "{")
        throw std::runtime_error("N2System: expected '{' after " + name.text);
      if (result.findChapter(name.text))
        throw std::runtime_error("N2System: duplicate chapter " + name.text);
      Chapter chapter;
      chapter.name = name.text;
      chapter.start = brace.end;
      result.chapters_.push_back(std::move(chapter));
      active = true;
      depth = 1;
      previous = {};
      continue;
    }
    // NsScript commands use call syntax, e.g. WaitKey() and Show(...).
    // Index calls without interpreting arguments or running them. Exclude
    // control-flow keywords whose parenthesized expressions are not calls.
    if (t.text == "(" && previous.kind == Kind::Identifier &&
        previous.text != "if" && previous.text != "while" &&
        previous.text != "for" && previous.text != "switch" &&
        previous.text != "catch") {
      result.chapters_.back().invocations.push_back(
          {previous.text, previous.offset, previous.line});
    }
    if (t.text == "{") {
      ++depth;
    } else if (t.text == "}") {
      --depth;
      if (depth == 0) {
        result.chapters_.back().end = t.offset;
        active = false;
      }
    } else if (depth == 1 && t.text == ":" &&
               previous.kind == Kind::Identifier && previous.line == t.line &&
               startsLine(source, previous.offset)) {
      auto &chapter = result.chapters_.back();
      if (chapter.findLabel(previous.text))
        throw std::runtime_error("N2System: duplicate label " + previous.text);
      chapter.labels.push_back({previous.text, previous.offset, previous.line});
    }
    previous = t;
  }
  if (active)
    throw std::runtime_error("N2System: unterminated chapter " +
                             result.chapters_.back().name);
  return result;
}
ScriptCursor::ScriptCursor(const ScriptIndex &index, std::string_view chapter)
    : index_(index) {
  if (!enterChapter(chapter))
    throw std::out_of_range("N2System: missing NsScript chapter");
}

bool ScriptCursor::enterChapter(std::string_view name) {
  const auto *chapter = index_.findChapter(name);
  if (!chapter)
    return false;
  current_ = chapter;
  next_ = 0;
  return true;
}

bool ScriptCursor::jumpToLabel(std::string_view name) {
  const auto *label = current_->findLabel(name);
  if (!label)
    return false;
  // Labels are source offsets, while invocation metadata is also ordered
  // by source offset. Never execute the next call while seeking.
  const auto &calls = current_->invocations;
  next_ = static_cast<std::size_t>(
      std::lower_bound(calls.begin(), calls.end(), label->offset,
                       [](const Invocation &call, std::size_t offset) {
                         return call.offset < offset;
                       }) -
      calls.begin());
  return true;
}

const Invocation *ScriptCursor::peek() const {
  return next_ < current_->invocations.size() ? &current_->invocations[next_]
                                              : nullptr;
}

const Invocation *ScriptCursor::advance() {
  const Invocation *call = peek();
  if (call)
    ++next_;
  return call;
}

std::size_t ScriptCursor::remaining() const {
  return current_->invocations.size() - next_;
}

bool ScriptCursor::seekPosition(std::size_t position) {
  if (position > current_->invocations.size())
    return false;
  next_ = position;
  return true;
}

std::string_view ScriptCursor::chapterName() const { return current_->name; }

} // namespace N2System
