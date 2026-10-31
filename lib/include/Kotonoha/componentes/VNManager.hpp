#pragma once

#include <Kotonoha/localization/Translation.hpp>

#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/Scene.hpp>
#include <Kotonoha/script/VNScript.hpp>

#include <functional>
#include <unordered_map>
#include <vector>

namespace Kotonoha {

class VNManager final : public Scene {
private:
  std::unordered_map<int, ScriptNode> scriptNodes;
  int currentNodeId = -1;
  bool isFinishedState = false;
  bool awaitingChoice = false;
  bool inputBlocked = false; // Bloqueia o avanço manual por entrada do usuário
  ScriptMode masterMode = ScriptMode::Standard;
  std::unordered_map<std::string, std::string> variables;
  ScriptNode localizedNode(int nodeId) const;
  TimelineScript timedScript(const ScriptNode &node) const;
  bool isTimedNode(const ScriptNode &node) const;

public:
  explicit VNManager(Engine &engineRef);

  void loadScript(const std::vector<ScriptNode> &nodes,
                  ScriptMode mode = ScriptMode::Standard);
  void startScript(int startNodeId = 0);
  void jumpToNode(int nodeId);

  void advance();
  ScriptMode getMode() const;
  ScriptMode getMasterMode() const { return masterMode; }
  VNSave captureSave(const std::string &gameData = "") const;
  void restoreSave(const VNSave &save);
  void save(const std::string &path, const std::string &gameData = "") const;
  void setVariable(const std::string &name, const std::string &value);

  void setInputBlocked(bool blocked) { inputBlocked = blocked; }
  bool isInputBlocked() const { return inputBlocked; }

  void onEvent(EventRef event) override;
  void update() override;

  bool isFinished() const { return isFinishedState; }
  bool isWaitingForChoice() const { return awaitingChoice; }
  int getCurrentNodeId() const { return currentNodeId; }

  ScriptNode &getScriptNode(int nodeId);
  std::vector<int> getNodeIds() const;
  const ScriptNode &getScriptNode(int nodeId) const;
  void dumpTranslation(const std::string &path) const;
  // Texts of the loaded script nodes keyed by node id (empty without a script).
  Translation::SourceTexts translationSources() const;
  void debugJumpToNode(int nodeId);
  void loadKtrf(const std::string &path);
  void dumpKtrf(const std::string &path) const;
};

VNManager &GetVNManagerRuntime(Engine &engineRef);

} // namespace Kotonoha