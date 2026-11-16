// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ScriptExecutor.hpp>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace N2System {
namespace {
class LiteralParser {
public:
  LiteralParser(std::string_view source, std::size_t start, std::size_t end)
      : source_(source), pos_(start), end_(end) {}
  std::size_t position() const { return pos_; }

  void trivia() {
    while (pos_ < end_) {
      const unsigned char c = static_cast<unsigned char>(source_[pos_]);
      if (std::isspace(c)) {
        ++pos_;
      } else if (c == '/' && pos_ + 1 < end_ && source_[pos_ + 1] == '/') {
        pos_ += 2;
        while (pos_ < end_ && source_[pos_] != '\r' && source_[pos_] != '\n')
          ++pos_;
      } else if (c == '/' && pos_ + 1 < end_ && source_[pos_ + 1] == '*') {
        pos_ += 2;
        while (pos_ + 1 < end_ &&
               !(source_[pos_] == '*' && source_[pos_ + 1] == '/'))
          ++pos_;
        if (pos_ + 1 >= end_)
          throw std::runtime_error("NsScript: unterminated comment");
        pos_ += 2;
      } else {
        return;
      }
    }
  }
  bool take(char ch) {
    trivia();
    if (pos_ < end_ && source_[pos_] == ch) {
      ++pos_;
      return true;
    }
    return false;
  }
  void expect(char ch) {
    if (!take(ch))
      throw std::runtime_error(
          "NsScript: unsupported expression or missing delimiter");
  }
  std::vector<std::string> arguments() {
    std::vector<std::string> result;
    expect('(');
    if (take(')'))
      return result;
    while (true) {
      if (result.size() >= 64)
        throw std::runtime_error("NsScript: too many literal arguments");
      result.push_back(literal());
      if (take(')'))
        break;
      expect(',');
    }
    return result;
  }

private:
  std::string literal() {
    trivia();
    if (pos_ >= end_)
      throw std::runtime_error("NsScript: expected literal");
    std::string value;
    const char quote = source_[pos_];
    if (quote == '"' || quote == '\'') {
      ++pos_;
      bool closed = false;
      while (pos_ < end_) {
        const char ch = source_[pos_++];
        if (ch == quote) {
          closed = true;
          break;
        }
        if (ch == '\\') {
          if (pos_ == end_)
            throw std::runtime_error("NsScript: incomplete escape");
          const char escaped = source_[pos_++];
          switch (escaped) {
          case 'n':
            value += '\n';
            break;
          case 'r':
            value += '\r';
            break;
          case 't':
            value += '\t';
            break;
          case '\\':
            value += '\\';
            break;
          case '"':
            value += '"';
            break;
          case '\'':
            value += '\'';
            break;
          default:
            throw std::runtime_error("NsScript: unsupported escape");
          }
        } else {
          value += ch;
        }
        if (value.size() > 65536)
          throw std::runtime_error("NsScript: oversized literal");
      }
      if (!closed)
        throw std::runtime_error("NsScript: unterminated string");
    } else {
      // Only decimal numeric literals are accepted; arbitrary identifiers
      // and expression operands must never execute as literal callbacks.
      if (quote == '-') {
        value += '-';
        ++pos_;
      }
      std::size_t digits = 0;
      while (pos_ < end_ && source_[pos_] >= '0' && source_[pos_] <= '9') {
        if (++digits > 19)
          throw std::runtime_error("NsScript: numeric literal too long");
        value += source_[pos_++];
      }
      if (!digits)
        throw std::runtime_error("NsScript: nonliteral argument");
    }
    return value;
  }
  std::string_view source_;
  std::size_t pos_, end_;
};
} // namespace

ScriptExecutor::ScriptExecutor(std::string source,
                               std::string_view initialChapter,
                               VmCallback callback)
    : source_(std::move(source)), index_(ScriptIndex::Parse(source_)),
      cursor_(index_, initialChapter), callback_(std::move(callback)) {
  if (!callback_)
    throw std::invalid_argument("NsScript: missing host command callback");
  validate(); // Reject unsupported control flow BEFORE first side effect.
}

void ScriptExecutor::validate() {
  for (const auto &chapter : index_.chapters()) {
    LiteralParser reader(source_, chapter.start, chapter.end);
    std::size_t call = 0, label = 0;
    while (true) {
      reader.trivia();
      const std::size_t offset = reader.position();
      if (offset == chapter.end)
        break;
      if (label < chapter.labels.size() &&
          chapter.labels[label].offset == offset) {
        const auto &name = chapter.labels[label++].name;
        for (std::size_t j = 0; j < name.size(); ++j)
          reader.expect(name[j]);
        reader.expect(':');
        continue;
      }
      if (call < chapter.invocations.size() &&
          chapter.invocations[call].offset == offset) {
        const auto &inv = chapter.invocations[call++];
        for (char ch : inv.function)
          reader.expect(ch);
        auto args = reader.arguments();
        reader.expect(';');
        arguments_.emplace(inv.offset, std::move(args));
        continue;
      }
      throw std::runtime_error(
          "NsScript: unsupported statement/control flow in chapter " +
          chapter.name + " at byte " + std::to_string(offset));
    }
    if (call != chapter.invocations.size() || label != chapter.labels.size())
      throw std::runtime_error("NsScript: non-standalone invocation/label");
  }
}

bool ScriptExecutor::jumpToLabel(std::string_view label) {
  if (!cursor_.jumpToLabel(label))
    return false;
  finished_ = paused_ = false;
  return true;
}
bool ScriptExecutor::enterChapter(std::string_view chapter) {
  if (!cursor_.enterChapter(chapter))
    return false;
  // This is an external hard navigation, not a subroutine call.
  callStack_.clear();
  finished_ = paused_ = false;
  return true;
}
std::size_t ScriptExecutor::run(std::size_t budget) {
  std::size_t executed = 0;
  while (!paused_ && !finished_ && executed < budget) {
    const auto *inv = cursor_.peek();
    if (!inv) {
      finished_ = true;
      break;
    }
    const auto it = arguments_.find(inv->offset);
    if (it == arguments_.end())
      throw std::runtime_error("NsScript: compiled invocation missing");
    // Preserve PC on callback errors and invalid navigation targets.
    const auto action = callback_(*inv, it->second);
    switch (action.kind) {
    case VmActionKind::Next:
      cursor_.advance();
      break;
    case VmActionKind::Pause:
      cursor_.advance();
      paused_ = true;
      break;
    case VmActionKind::Finish:
      cursor_.advance();
      callStack_.clear();
      finished_ = true;
      break;
    case VmActionKind::JumpLabel:
      if (!cursor_.jumpToLabel(action.target))
        throw std::runtime_error("NsScript: missing jump label " +
                                 action.target);
      break;
    case VmActionKind::EnterChapter:
      if (!cursor_.enterChapter(action.target))
        throw std::runtime_error("NsScript: missing destination chapter " +
                                 action.target);
      callStack_.clear();
      break;
    case VmActionKind::CallLabel:
    case VmActionKind::CallChapter: {
      if (callStack_.size() >= MaxCallDepth)
        throw std::runtime_error("NsScript: subroutine depth limit exceeded");
      const bool local = action.kind == VmActionKind::CallLabel;
      const auto *chapter = index_.findChapter(cursor_.chapterName());
      const bool found = local ? (chapter && chapter->findLabel(action.target))
                               : (index_.findChapter(action.target) != nullptr);
      if (!found)
        throw std::runtime_error("NsScript: missing subroutine " +
                                 action.target);
      // Validate the target before modifying any VM state. Allocate the
      // return frame first so an allocation failure preserves the cursor.
      callStack_.push_back(
          {std::string(cursor_.chapterName()), cursor_.position() + 1});
      if (local)
        cursor_.jumpToLabel(action.target);
      else
        cursor_.enterChapter(action.target);
      break;
    }
    case VmActionKind::Return: {
      if (callStack_.empty())
        throw std::runtime_error("NsScript: return without a call");
      const auto &frame = callStack_.back();
      if (!cursor_.enterChapter(frame.chapter) ||
          !cursor_.seekPosition(frame.position))
        throw std::runtime_error("NsScript: invalid saved return frame");
      callStack_.pop_back();
      break;
    }
    }
    ++executed;
  }
  if (!paused_ && !cursor_.peek())
    finished_ = true;
  return executed;
}
} // namespace N2System
