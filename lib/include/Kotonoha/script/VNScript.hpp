#pragma once

#include <Kotonoha/script/Timeline.hpp>
#include <functional>
#include <string>
#include <vector>

namespace Kotonoha {

enum class NodeType { Dialogue, Choice, Action, Conditional, Timeline, End };

struct ChoiceOptionData {
  std::string label;
  int targetNodeId = -1;
};

struct ScriptNode {
  int id = -1;
  NodeType type = NodeType::Dialogue;

  std::string speaker;
  std::string text;
  std::string backgroundAsset;
  std::string characterAsset;

  std::string choiceQuestion;
  std::vector<ChoiceOptionData> choices;

  // Lógica de Ação e Condicionais
  std::function<void()> actionCallback = nullptr;
  std::function<int()> conditionCallback = nullptr;

  int nextNodeId = -1;
  TimelineScript timeline;
  double duration = 0;
  ChoiceTimeout timeout = ChoiceTimeout::Pause;
  int timeoutTargetNodeId = -1;
  std::string name;

  ScriptNode() = default;

  ScriptNode(int id, TimelineScript timeline, int nextNodeId = -1)
      : id(id), type(NodeType::Timeline), nextNodeId(nextNodeId),
        timeline(std::move(timeline)) {}

  // Construtor para Diálogo
  ScriptNode(int id, NodeType type, std::string speaker, std::string text,
             std::string bg, std::string character,
             std::vector<ChoiceOptionData> choices, int nextNodeId)
      : id(id), type(type), speaker(std::move(speaker)), text(std::move(text)),
        backgroundAsset(std::move(bg)), characterAsset(std::move(character)),
        choices(std::move(choices)), nextNodeId(nextNodeId) {}

  // Construtor para Escolha
  ScriptNode(int id, NodeType type, std::string speaker, std::string text,
             std::string bg, std::string character, std::string question,
             std::vector<ChoiceOptionData> choices, int nextNodeId)
      : id(id), type(type), speaker(std::move(speaker)), text(std::move(text)),
        backgroundAsset(std::move(bg)), characterAsset(std::move(character)),
        choiceQuestion(std::move(question)), choices(std::move(choices)),
        nextNodeId(nextNodeId) {}

  // Construtor para Nó de Ação / Execução de Script
  ScriptNode(int id, NodeType type, std::function<void()> action,
             int nextNodeId)
      : id(id), type(type), actionCallback(std::move(action)),
        nextNodeId(nextNodeId) {}

  // Construtor para Nó Condicional (Bifurcação por código)
  ScriptNode(int id, NodeType type, std::function<int()> condition)
      : id(id), type(type), conditionCallback(std::move(condition)) {}
};

} // namespace Kotonoha