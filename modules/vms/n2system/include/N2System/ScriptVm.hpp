// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

#include <N2System/AudioSystem.hpp>
#include <N2System/DialogueSystem.hpp>
#include <N2System/SaveState.hpp>
#include <N2System/SceneGraph.hpp>
#include <N2System/ScriptExecutor.hpp>
#include <N2System/VariableStore.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace N2System {

class ScriptVm {
public:
  ScriptVm() = default;

  void loadScript(std::string source, std::string_view initialChapter = "main");

  std::size_t step(std::size_t budget = 8192);
  void update(float dtSeconds);

  bool advance();

  bool paused() const;
  bool finished() const;
  void resume();

  VariableStore &variables() { return variables_; }
  const VariableStore &variables() const { return variables_; }

  SceneGraph &sceneGraph() { return sceneGraph_; }
  const SceneGraph &sceneGraph() const { return sceneGraph_; }

  AudioSystem &audioSystem() { return audioSystem_; }
  const AudioSystem &audioSystem() const { return audioSystem_; }

  DialogueSystem &dialogueSystem() { return dialogueSystem_; }
  const DialogueSystem &dialogueSystem() const { return dialogueSystem_; }

  bool hasPendingChoice() const;
  std::vector<std::string> pendingChoiceOptions() const;
  void selectChoice(int index);

  SaveData saveState() const;
  bool loadState(const SaveData &data);

  std::string_view currentChapter() const;
  // Debug-only validated label navigation within the current NsScript chapter.
  bool debugJumpToLabel(std::string_view label);

private:
  VmAction handleInvocation(const Invocation &inv, const std::vector<std::string> &args);

  VariableStore variables_;
  SceneGraph sceneGraph_;
  AudioSystem audioSystem_;
  DialogueSystem dialogueSystem_;

  std::string source_;
  std::unique_ptr<ScriptExecutor> executor_;
};

} // namespace N2System
