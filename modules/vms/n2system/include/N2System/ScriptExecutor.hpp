// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include <N2System/ScriptIndex.hpp>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace N2System {
// A deliberately small, executable NsScript subset. Compile-time validation
// allows only direct, semicolon-terminated calls with literal arguments and
// line-leading labels. Conditions, loops, assignments, nested expressions
// and arbitrary source statements fail CLOSED instead of being replayed as
// unconditional calls from the metadata index.
enum class VmActionKind {
  Next, Pause, Finish, JumpLabel, EnterChapter,
  CallLabel, CallChapter, Return
};
struct VmAction {
  VmActionKind kind = VmActionKind::Next;
  std::string target;
};
using VmCallback = std::function<VmAction(
    const Invocation &, const std::vector<std::string> &)>;

class ScriptExecutor {
public:
  ScriptExecutor(std::string source, std::string_view initialChapter,
                 VmCallback callback);
  // ScriptCursor borrows our ScriptIndex. Implicit copying/moving would
  // leave it referencing the previous object's index (possibly destroyed).
  ScriptExecutor(const ScriptExecutor &) = delete;
  ScriptExecutor &operator=(const ScriptExecutor &) = delete;
  ScriptExecutor(ScriptExecutor &&) = delete;
  ScriptExecutor &operator=(ScriptExecutor &&) = delete;
  // Each callback dispatch counts toward budget. An invalid target leaves
  // the current instruction unchanged. Pause requires explicit resume().
  std::size_t run(std::size_t budget = 8192);
  bool paused() const { return paused_; }
  bool finished() const { return finished_; }
  void resume() { paused_ = false; }
  const Invocation *next() const { return cursor_.peek(); }
  std::string_view chapterName() const { return cursor_.chapterName(); }
  std::size_t remaining() const { return cursor_.remaining(); }
  bool jumpToLabel(std::string_view label);
  bool enterChapter(std::string_view chapter);
  std::size_t callDepth() const { return callStack_.size(); }
private:
  void validate();
  std::string source_;
  ScriptIndex index_;
  ScriptCursor cursor_;
  VmCallback callback_;
  std::map<std::size_t, std::vector<std::string>> arguments_;
  struct ReturnFrame {
    std::string chapter;
    std::size_t position = 0;
  };
  std::vector<ReturnFrame> callStack_;
  static constexpr std::size_t MaxCallDepth = 1024;
  bool paused_ = false, finished_ = false;
};
} // namespace N2System
