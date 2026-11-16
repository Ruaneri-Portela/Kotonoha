// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace N2System {
// Non-executing NsScript metadata for future N2System VM integration.
struct Label {
  std::string name;
  std::size_t offset = 0;
  unsigned line = 0;
};
struct Invocation {
  std::string function;
  std::size_t offset = 0;
  unsigned line = 0;
};
struct Chapter {
  std::string name;
  std::size_t start = 0; // just after opening brace
  std::size_t end = 0;   // matching closing brace
  std::vector<Label> labels;
  // Function invocations in script order, without executing script actions.
  // Includes expression operands such as ScrollbarValue("Scroll").
  std::vector<Invocation> invocations;
  const Label *findLabel(std::string_view name) const;
};
class ScriptIndex {
public:
  // Recognizes "chapter NAME { ... }" and line-leading "label:".
  // Ignores comments and string literals when counting braces.
  // Throws std::runtime_error on malformed chapter structure.
  static ScriptIndex Parse(std::string_view source);
  const std::vector<Chapter> &chapters() const { return chapters_; }
  const Chapter *findChapter(std::string_view name) const;
private:
  std::vector<Chapter> chapters_;
};

// Read-only traversal of NsScript call sites for debugging and future VM
// callbacks. This does NOT evaluate expressions, branches, waits or functions.
// The referenced ScriptIndex must outlive the cursor.
class ScriptCursor {
public:
  ScriptCursor(const ScriptIndex &index, std::string_view chapter);
  // Failed navigation does not change the current chapter or position.
  bool enterChapter(std::string_view chapter);
  bool jumpToLabel(std::string_view label);
  const Invocation *peek() const;
  const Invocation *advance();
  std::size_t remaining() const;
  std::size_t position() const { return next_; }
  // Reposition by invocation ordinal for exact subroutine returns.
  // Unlike source-text labels this may designate the end of a chapter.
  bool seekPosition(std::size_t position);
  std::string_view chapterName() const;
private:
  const ScriptIndex &index_;
  const Chapter *current_ = nullptr;
  std::size_t next_ = 0;
};
} // namespace N2System
