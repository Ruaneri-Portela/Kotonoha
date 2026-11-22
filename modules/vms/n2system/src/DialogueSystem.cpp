// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/DialogueSystem.hpp>
#include <algorithm>
#include <utility>

namespace N2System {

void DialogueSystem::setDialogue(std::string_view speaker,
                                 std::string_view text,
                                 std::string_view voicePath) {
  speaker_ = std::string(speaker);
  text_ = std::string(text);
  voicePath_ = std::string(voicePath);

  visibleCharCount_ = 0;
  typewriterElapsed_ = 0.0f;
  isTyping_ = !text_.empty();
  isWaitingForKey_ = false;

  if (!text_.empty()) {
    backlog_.push_back({speaker_, text_, voicePath_});
    if (backlog_.size() > 500)
      backlog_.erase(backlog_.begin());
  }
}

void DialogueSystem::restore(
    std::string speaker, std::string text, std::string voice,
    std::vector<DialogueBacklogEntry> backlog, std::size_t visibleChars,
    float charsPerSecond, bool typing, bool waiting) {
  speaker_ = std::move(speaker);
  text_ = std::move(text);
  voicePath_ = std::move(voice);
  if (backlog.size() > 500)
    backlog.erase(backlog.begin(), backlog.end() - 500);
  backlog_ = std::move(backlog);
  visibleCharCount_ = std::min(visibleChars, text_.size());
  typewriterSpeed_ = std::max(1.0f, charsPerSecond);
  typewriterElapsed_ = float(visibleCharCount_) / typewriterSpeed_;
  isTyping_ = typing && visibleCharCount_ < text_.size();
  isWaitingForKey_ = waiting;
}

void DialogueSystem::clearDialogue() {
  speaker_.clear();
  text_.clear();
  voicePath_.clear();
  visibleCharCount_ = 0;
  isTyping_ = false;
  isWaitingForKey_ = false;
}

void DialogueSystem::finishTyping() {
  visibleCharCount_ = text_.size();
  isTyping_ = false;
}

void DialogueSystem::setWaitKey() {
  finishTyping();
  isWaitingForKey_ = true;
}

bool DialogueSystem::advance() {
  if (isTyping_) {
    finishTyping();
    return true;
  }
  if (isWaitingForKey_) {
    isWaitingForKey_ = false;
    return true;
  }
  return false;
}

void DialogueSystem::update(float dtSeconds) {
  if (isTyping_ && typewriterSpeed_ > 0.0f) {
    typewriterElapsed_ += dtSeconds;
    std::size_t count =
        static_cast<std::size_t>(typewriterElapsed_ * typewriterSpeed_);
    if (count >= text_.size()) {
      visibleCharCount_ = text_.size();
      isTyping_ = false;
    } else {
      visibleCharCount_ = count;
    }
  }
}

std::string DialogueSystem::visibleText() const {
  if (visibleCharCount_ >= text_.size())
    return text_;

  // Ensure UTF-8 character boundary safety
  std::size_t byteIdx = 0;
  std::size_t charIdx = 0;
  while (byteIdx < text_.size() && charIdx < visibleCharCount_) {
    unsigned char c = static_cast<unsigned char>(text_[byteIdx]);
    if ((c & 0x80) == 0)
      byteIdx += 1;
    else if ((c & 0xE0) == 0xC0)
      byteIdx += 2;
    else if ((c & 0xF0) == 0xE0)
      byteIdx += 3;
    else if ((c & 0xF8) == 0xF0)
      byteIdx += 4;
    else
      byteIdx += 1;
    charIdx++;
  }
  return text_.substr(0, std::min(byteIdx, text_.size()));
}

} // namespace N2System
