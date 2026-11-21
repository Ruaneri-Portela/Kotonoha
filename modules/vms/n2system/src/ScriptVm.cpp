// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include <N2System/ScriptVm.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace N2System {

void ScriptVm::loadScript(std::string source, std::string_view initialChapter) {
  source_ = std::move(source);
  executor_ = std::make_unique<ScriptExecutor>(
      source_, initialChapter,
      [this](const Invocation &inv, const std::vector<std::string> &args) {
        return handleInvocation(inv, args);
      });
}

std::size_t ScriptVm::step(std::size_t budget) {
  if (!executor_)
    return 0;
  if (dialogueSystem_.isWaitingForKey())
    return 0;
  return executor_->run(budget);
}

void ScriptVm::update(float dtSeconds) {
  sceneGraph_.update(dtSeconds);
  audioSystem_.update(dtSeconds);
  dialogueSystem_.update(dtSeconds);
}

bool ScriptVm::advance() {
  if (dialogueSystem_.advance()) {
    if (!dialogueSystem_.isWaitingForKey() && !dialogueSystem_.isTyping()) {
      resume();
    }
    return true;
  }
  return false;
}

bool ScriptVm::paused() const {
  return executor_ ? executor_->paused() : false;
}

bool ScriptVm::finished() const {
  return executor_ ? executor_->finished() : true;
}

void ScriptVm::resume() {
  if (executor_)
    executor_->resume();
}

std::string_view ScriptVm::currentChapter() const {
  return executor_ ? executor_->chapterName() : "";
}

bool ScriptVm::debugJumpToLabel(std::string_view label) {
  if (!executor_ || !executor_->jumpToLabel(label))
    return false;
  dialogueSystem_.clearDialogue();
  executor_->resume();
  return true;
}

bool ScriptVm::hasPendingChoice() const {
  for (const auto &[name, entity] : sceneGraph_.entities()) {
    if (entity.type == EntityType::Choice && !entity.choiceOptions.empty())
      return true;
  }
  return false;
}

std::vector<std::string> ScriptVm::pendingChoiceOptions() const {
  for (const auto &[name, entity] : sceneGraph_.entities()) {
    if (entity.type == EntityType::Choice && !entity.choiceOptions.empty())
      return entity.choiceOptions;
  }
  return {};
}

void ScriptVm::selectChoice(int index) {
  std::string choiceEntityName;
  std::string selectedOption;
  for (const auto &[name, entity] : sceneGraph_.entities()) {
    if (entity.type == EntityType::Choice && !entity.choiceOptions.empty()) {
      choiceEntityName = name;
      if (index >= 0 &&
          static_cast<std::size_t>(index) < entity.choiceOptions.size())
        selectedOption = entity.choiceOptions[index];
      break;
    }
  }

  if (!choiceEntityName.empty()) {
    sceneGraph_.destroyEntity(choiceEntityName);
  }

  variables_.setInt("$selected_choice", index);
  variables_.setString("@selected_option", selectedOption);
  resume();
}

VmAction ScriptVm::handleInvocation(const Invocation &inv,
                                    const std::vector<std::string> &args) {
  const auto &func = inv.function;

  // Jump / Subroutine Flow Control
  if (func == "Goto" || func == "Jump") {
    if (!args.empty())
      return {VmActionKind::JumpLabel, args[0]};
  }
  if (func == "Chapter") {
    if (!args.empty())
      return {VmActionKind::EnterChapter, args[0]};
  }
  if (func == "Call") {
    if (!args.empty())
      return {VmActionKind::CallChapter, args[0]};
  }
  if (func == "CallLocal") {
    if (!args.empty())
      return {VmActionKind::CallLabel, args[0]};
  }
  if (func == "Return") {
    return {VmActionKind::Return, {}};
  }
  if (func == "End" || func == "Finish") {
    return {VmActionKind::Finish, {}};
  }

  const auto resolveStr = [this](const std::string &arg) -> std::string {
    if (!arg.empty() && (arg[0] == '@' || arg[0] == '$' || arg[0] == '#' ||
                         arg[0] == '"' || arg[0] == '\'')) {
      return variables_.evaluateString(arg);
    }
    return arg;
  };

  // Dialogue Commands
  if (func == "SetDialogue" || func == "Dialogue" || func == "Talk" ||
      func == "Message") {
    std::string speaker, text, voice;
    if (args.size() == 1) {
      text = resolveStr(args[0]);
    } else if (args.size() == 2) {
      speaker = resolveStr(args[0]);
      text = resolveStr(args[1]);
    } else if (args.size() >= 3) {
      speaker = resolveStr(args[0]);
      text = resolveStr(args[1]);
      voice = resolveStr(args[2]);
    }
    dialogueSystem_.setDialogue(speaker, text, voice);
    if (!voice.empty())
      audioSystem_.playVoice(voice);
    return {VmActionKind::Next, {}};
  }

  if (func == "ClearDialogue" || func == "ClearMessage") {
    dialogueSystem_.clearDialogue();
    return {VmActionKind::Next, {}};
  }

  if (func == "WaitKey" || func == "WaitClick") {
    dialogueSystem_.setWaitKey();
    return {VmActionKind::Pause, {}};
  }

  // Graphics & Entities
  if (func == "CreateSprite" || func == "LoadImage" ||
      func == "CreateTexture") {
    if (!args.empty()) {
      std::string name = args[0];
      std::string path =
          args.size() > 1 ? variables_.evaluateString(args[1]) : "";
      auto *e = sceneGraph_.createEntity(name, EntityType::Sprite);
      e->imagePath = path;
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "CreateText") {
    if (!args.empty()) {
      std::string name = args[0];
      std::string text =
          args.size() > 1 ? variables_.evaluateString(args[1]) : "";
      auto *e = sceneGraph_.createEntity(name, EntityType::Text);
      e->text = text;
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "CreateChoice" || func == "SelectChoice" || func == "Choice") {
    if (!args.empty()) {
      std::string name = args[0];
      auto *e = sceneGraph_.createEntity(name, EntityType::Choice);
      for (std::size_t i = 1; i < args.size(); ++i) {
        e->choiceOptions.push_back(variables_.evaluateString(args[i]));
      }
    }
    return {VmActionKind::Pause, {}};
  }

  if (func == "SetPosition" || func == "MovePosition") {
    if (args.size() >= 3) {
      std::string name = args[0];
      float x = static_cast<float>(variables_.evaluateInt(args[1]));
      float y = static_cast<float>(variables_.evaluateInt(args[2]));
      float duration = args.size() >= 4
                           ? static_cast<float>(variables_.evaluateInt(args[3]))
                           : 0.0f;
      sceneGraph_.moveEntity(name, x, y, duration);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "SetOpacity" || func == "Fade") {
    if (args.size() >= 2) {
      std::string name = args[0];
      float opacity =
          static_cast<float>(variables_.evaluateInt(args[1])) / 255.0f;
      if (opacity > 1.0f)
        opacity = 1.0f;
      float duration = args.size() >= 3
                           ? static_cast<float>(variables_.evaluateInt(args[2]))
                           : 0.0f;
      sceneGraph_.fadeEntity(name, opacity, duration);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "SetScale" || func == "Zoom") {
    if (args.size() >= 3) {
      std::string name = args[0];
      float sx = static_cast<float>(variables_.evaluateInt(args[1])) / 100.0f;
      float sy = static_cast<float>(variables_.evaluateInt(args[2])) / 100.0f;
      float duration = args.size() >= 4
                           ? static_cast<float>(variables_.evaluateInt(args[3]))
                           : 0.0f;
      sceneGraph_.zoomEntity(name, sx, sy, duration);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "SetAlias") {
    if (args.size() >= 2) {
      sceneGraph_.setAlias(args[0], args[1]);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "Destroy" || func == "Delete") {
    if (!args.empty()) {
      sceneGraph_.destroyEntity(args[0]);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "Clear") {
    sceneGraph_.clear();
    return {VmActionKind::Next, {}};
  }

  // Audio Commands
  if (func == "PlayBgm" || func == "Bgm") {
    if (!args.empty()) {
      std::string path = variables_.evaluateString(args[0]);
      float fade = args.size() > 1
                       ? static_cast<float>(variables_.evaluateInt(args[1]))
                       : 0.0f;
      audioSystem_.playBgm(path, true, fade);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "StopBgm") {
    float fade = !args.empty()
                     ? static_cast<float>(variables_.evaluateInt(args[0]))
                     : 0.0f;
    audioSystem_.stopBgm(fade);
    return {VmActionKind::Next, {}};
  }

  if (func == "PlayVoice" || func == "Voice") {
    if (!args.empty()) {
      std::string path = variables_.evaluateString(args[0]);
      audioSystem_.playVoice(path);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "StopVoice") {
    audioSystem_.stopVoice();
    return {VmActionKind::Next, {}};
  }

  if (func == "PlaySE" || func == "SE") {
    if (!args.empty()) {
      std::string path = variables_.evaluateString(args[0]);
      int ch = args.size() > 1
                   ? static_cast<int>(variables_.evaluateInt(args[1]))
                   : 0;
      audioSystem_.playSe(path, ch);
    }
    return {VmActionKind::Next, {}};
  }

  // Variable Assignments & Setters
  if (func == "Set" || func == "SetVariable" || func == "VarSet") {
    if (args.size() >= 2) {
      std::string varName = args[0];
      auto val = variables_.evaluate(args[1]);
      variables_.set(varName, val);
    }
    return {VmActionKind::Next, {}};
  }

  if (func == "SetFlag") {
    if (args.size() >= 2) {
      std::string flagName = args[0];
      bool val = variables_.evaluateBool(args[1]);
      variables_.setFlag(flagName, val);
    }
    return {VmActionKind::Next, {}};
  }

  return {VmActionKind::Next, {}};
}

SaveData ScriptVm::saveState() const {
  SaveData data;
  data.chapterName = std::string(currentChapter());
  data.position = executor_ ? executor_->remaining() : 0;
  data.integers = variables_.integers();
  data.strings = variables_.strings();
  data.flags = variables_.flags();
  data.activeSpeaker = std::string(dialogueSystem_.speaker());
  data.activeText = std::string(dialogueSystem_.fullText());
  data.activeVoice = std::string(dialogueSystem_.voicePath());
  data.backlog = dialogueSystem_.backlog();
  return data;
}

bool ScriptVm::loadState(const SaveData &data) {
  if (data.chapterName.empty() || !executor_)
    return false;

  variables_.clear();
  for (const auto &[k, v] : data.integers)
    variables_.setInt(k, v);
  for (const auto &[k, v] : data.strings)
    variables_.setString(k, v);
  for (const auto &[k, v] : data.flags)
    variables_.setFlag(k, v);

  dialogueSystem_.setDialogue(data.activeSpeaker, data.activeText,
                              data.activeVoice);

  return executor_->enterChapter(data.chapterName);
}

} // namespace N2System
