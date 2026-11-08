// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once
#include "RealLive/Script.hpp"
#include <functional>
#include <map>
#include <vector>
namespace RealLive {
struct GlossCursor {
  int x = 53, y = 364, left = 53, right = 581, lineHeight = 28;
};
struct GlossRectangle {
  int left = 0, top = 0, right = 0, bottom = 0;
};
class Session {
  ScriptArchive &archive_;
  RLMachine machine_;
  struct Frame {
    int scene;
    std::size_t pc;
  };
  std::vector<Frame> stack_;
  std::map<int, std::vector<const libreallive::BytecodeElement *>> scripts_;
  int scene_ = 0;
  std::size_t pc_ = 0;
  bool blocked_ = false, finished_ = false;
  const std::vector<const libreallive::BytecodeElement *> &script(int);
  void execute(const libreallive::CommandElement &);
  bool flow(const libreallive::CommandElement &);
  bool strings(const libreallive::CommandElement &);
  bool memory(const libreallive::CommandElement &);
  bool babel(const libreallive::CommandElement &);
  std::string &babelString(int address);
  int babelGetChar(int output, int xmod);
  int babelAppend(int address);
  std::string babelBuffer_;
  std::size_t babelIndex_ = 0;
  unsigned babelStarts_ = 0;
  bool babelItalic_ = false;
  std::map<int, int> babelNameModes_;
  std::vector<std::string> babelGlosses_;
  GlossCursor glossStart_;
  std::vector<std::vector<GlossRectangle>> glossRectangles_;
  void target(const libreallive::CommandElement &, int, bool);

public:
  // True consumes an external command; false leaves its PC blocked for retry.
  std::function<bool(const libreallive::CommandElement &)> command;
  std::function<void(std::string)> text;
  std::function<void(int)> babelControl;
  std::function<void(std::string)> speaker;
  std::function<GlossCursor()> glossCursor;
  std::function<void(int, const std::vector<std::string> &)> selectionText;
  // Return the horizontal cursor position AFTER the raw encoded glyph.
  std::function<int(const std::string &)> glyphPosition;
  // Return 3 for a new line, or 4 when the page is full.
  std::function<int()> lineBreak;
  std::function<std::string(bool, int)> resolveName;
  int currentWindow = 0;
  explicit Session(ScriptArchive &, int start = 9030);
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  bool step();
  std::size_t run(std::size_t budget);
  void jump(int scenario, int entrypoint = 0);
  void seek(int scenario, std::size_t index);
  int scenario() const { return scene_; }
  std::size_t index() const { return pc_; }
  RLMachine &machine() { return machine_; }
  bool blocked() const { return blocked_; }
  bool finished() const { return finished_; }
  void resume() { blocked_ = false; }
  std::size_t stackDepth() const { return stack_.size(); }
  std::string snapshot() const;
  void restore(const std::string &data);
};
} // namespace RealLive
