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
struct Chapter {
  std::string name;
  std::size_t start = 0; // just after opening brace
  std::size_t end = 0;   // matching closing brace
  std::vector<Label> labels;
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
} // namespace N2System
