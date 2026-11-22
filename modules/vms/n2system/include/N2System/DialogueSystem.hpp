// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace N2System {

struct DialogueBacklogEntry {
  std::string speaker;
  std::string text;
  std::string voicePath;
};

class DialogueSystem {
public:
  DialogueSystem() = default;

  void setDialogue(std::string_view speaker, std::string_view text, std::string_view voicePath = "");
  void clearDialogue();

  void finishTyping();
  bool isTyping() const { return isTyping_; }

  void setWaitKey();
  bool isWaitingForKey() const { return isWaitingForKey_; }

  // Returns true if user advance action was consumed.
  bool advance();

  void update(float dtSeconds);

  std::string_view speaker() const { return speaker_; }
  std::string_view fullText() const { return text_; }
  std::string visibleText() const;
  std::string_view voicePath() const { return voicePath_; }

  std::size_t visibleCharCount() const { return visibleCharCount_; }
  std::size_t totalCharCount() const { return text_.size(); }

  float typewriterSpeed() const { return typewriterSpeed_; }
  void setTypewriterSpeed(float charsPerSec) { typewriterSpeed_ = charsPerSec; }

  const std::vector<DialogueBacklogEntry> &backlog() const { return backlog_; }
  void clearBacklog() { backlog_.clear(); }
  // Restore exactly, without adding a duplicate backlog entry or restarting
  // the typewriter from the beginning.
  void restore(std::string speaker, std::string text, std::string voice,
               std::vector<DialogueBacklogEntry> backlog,
               std::size_t visibleChars, float charsPerSecond,
               bool typing, bool waiting);

private:
  std::string speaker_;
  std::string text_;
  std::string voicePath_;

  std::size_t visibleCharCount_ = 0;
  float typewriterSpeed_ = 30.0f; // characters per second
  float typewriterElapsed_ = 0.0f;

  bool isTyping_ = false;
  bool isWaitingForKey_ = false;

  std::vector<DialogueBacklogEntry> backlog_;
};

} // namespace N2System
