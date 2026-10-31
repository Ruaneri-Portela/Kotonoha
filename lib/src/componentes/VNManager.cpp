#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>
#include <Kotonoha/script/KtrfScript.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Kotonoha {

VNManager::VNManager(Engine &engineRef) : Scene(engineRef) {}

bool VNManager::isTimedNode(const ScriptNode &node) const {
  return node.type == NodeType::Timeline ||
         (masterMode == ScriptMode::Timed &&
          (node.type == NodeType::Dialogue || node.type == NodeType::Choice));
}

ScriptMode VNManager::getMode() const {
  const auto found = scriptNodes.find(currentNodeId);
  return found != scriptNodes.end() && isTimedNode(found->second)
             ? ScriptMode::Timed
             : ScriptMode::Standard;
}

ScriptNode VNManager::localizedNode(int nodeId) const {
  auto node = scriptNodes.at(nodeId);
  const auto &translation = engine.GetTranslation();
  auto text = [&](const std::string &value) {
    const auto translated = translation.text(nodeId, value);
    std::string result;
    for (std::size_t offset = 0; offset < translated.size();) {
      if (translated[offset] == '{') {
        const auto end = translated.find('}', offset + 1);
        if (end != std::string::npos) {
          const auto variable =
              variables.find(translated.substr(offset + 1, end - offset - 1));
          if (variable != variables.end()) {
            result += variable->second;
            offset = end + 1;
            continue;
          }
        }
      }
      result += translated[offset++];
    }
    return result;
  };
  node.speaker = text(node.speaker);
  node.text = text(node.text);
  node.choiceQuestion = text(node.choiceQuestion);
  for (auto &choice : node.choices)
    choice.label = text(choice.label);
  for (auto &cue : node.timeline.cues) {
    if (cue.type != CueType::Video && cue.type != CueType::Audio)
      cue.value = text(cue.value);
    cue.speaker = text(cue.speaker);
    for (auto &choice : cue.choices)
      choice.label = text(choice.label);
  }
  return node;
}

void VNManager::setVariable(const std::string &name, const std::string &value) {
  if (name.empty() || name.find_first_of("{}") != std::string::npos)
    throw std::invalid_argument("Invalid script variable name");
  variables[name] = value;
}

TimelineScript VNManager::timedScript(const ScriptNode &node) const {
  if (node.type == NodeType::Timeline)
    return node.timeline;
  TimelineCue cue;
  cue.duration = node.duration;
  cue.type =
      node.type == NodeType::Choice ? CueType::Choice : CueType::Dialogue;
  cue.value = node.type == NodeType::Choice ? node.choiceQuestion : node.text;
  cue.speaker = node.speaker;
  cue.timeout = node.timeout;
  cue.timeoutTargetNodeId = node.timeoutTargetNodeId;
  for (const auto &choice : node.choices)
    cue.choices.push_back({choice.label, choice.targetNodeId});
  return {node.duration, {std::move(cue)}};
}

void VNManager::loadScript(const std::vector<ScriptNode> &nodes,
                           ScriptMode mode) {
  if (mode != ScriptMode::Standard && mode != ScriptMode::Timed)
    throw std::invalid_argument("Unknown master script mode");
  std::unordered_map<int, ScriptNode> validated;
  for (const auto &node : nodes) {
    if (node.type != NodeType::Dialogue && node.type != NodeType::Choice &&
        node.type != NodeType::Action && node.type != NodeType::Conditional &&
        node.type != NodeType::Timeline && node.type != NodeType::End)
      throw std::invalid_argument("Unknown script node type");
    if (node.id < 0 || !validated.emplace(node.id, node).second)
      throw std::invalid_argument(
          "Script node IDs must be unique and non-negative");
    if (node.type == NodeType::Choice && node.choices.empty())
      throw std::invalid_argument("Choice node requires options");
    if (node.type == NodeType::Timeline)
      node.timeline.validate();
    else if (mode == ScriptMode::Timed &&
             (node.type == NodeType::Dialogue || node.type == NodeType::Choice))
      timedScript(node).validate();
  }
  auto checkTarget = [&](int id) {
    if (id != -1 && validated.count(id) == 0)
      throw std::invalid_argument("Unknown script target ID: " +
                                  std::to_string(id));
  };
  for (const auto &node : nodes) {
    checkTarget(node.nextNodeId);
    for (const auto &choice : node.choices)
      checkTarget(choice.targetNodeId);
    if (node.type == NodeType::Choice && node.timeout == ChoiceTimeout::Select)
      checkTarget(node.timeoutTargetNodeId);
    for (const auto &cue : node.timeline.cues) {
      for (const auto &option : cue.choices)
        checkTarget(option.targetNodeId);
      if (cue.type == CueType::Choice && cue.timeout == ChoiceTimeout::Select)
        checkTarget(cue.timeoutTargetNodeId);
    }
  }
  engine.GetTimeline().stop();
  scriptNodes = std::move(validated);
  variables.clear();
  masterMode = mode;
  currentNodeId = -1;
  isFinishedState = true;
}

void VNManager::startScript(int startNodeId) {
  engine.GetTimeline().stop();
  isFinishedState = false;
  awaitingChoice = inputBlocked = false;
  jumpToNode(startNodeId);
}

void VNManager::jumpToNode(int nodeId) {
  auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
  auto &choices = GetChoiceQuestionRuntime(engine);
  engine.GetTimeline().stop();
  for (int steps = 0; steps < 1024; ++steps) {
    if (nodeId == -1) {
      box.hide();
      choices.hide();
      isFinishedState = true;
      inputBlocked = awaitingChoice = false;
      currentNodeId = -1;
      return;
    }
    const auto found = scriptNodes.find(nodeId);
    if (found == scriptNodes.end())
      throw std::out_of_range("Unknown script node: " + std::to_string(nodeId));
    currentNodeId = nodeId;
    isFinishedState = false;
    inputBlocked = awaitingChoice = false;
    // Callbacks may mutate the graph or jump to another node.
    const auto node = localizedNode(nodeId);
    box.hide();
    choices.hide();
    if (isTimedNode(node)) {
      inputBlocked = true;
      engine.GetTimeline().play(timedScript(node));
      engine.GetTimeline().setPaused(engine.isStoryPaused());
      awaitingChoice = GetChoiceQuestionRuntime(engine).isActive();
      return;
    }
    switch (node.type) {
    case NodeType::Dialogue:
      box.setDialogue(node.speaker, node.text);
      box.show();
      return;
    case NodeType::Choice: {
      awaitingChoice = inputBlocked = true;
      std::vector<ChoiceOption> options;
      for (const auto &choice : node.choices)
        options.push_back({choice.label, choice.targetNodeId});
      choices.setQuestion(
          node.choiceQuestion, options,
          [this](int selected, const std::string &) { jumpToNode(selected); });
      return;
    }
    case NodeType::Timeline:
      inputBlocked = true;
      engine.GetTimeline().play(node.timeline);
      return;
    case NodeType::Action:
      if (node.actionCallback)
        node.actionCallback();
      if (currentNodeId != nodeId || inputBlocked)
        return;
      nodeId = node.nextNodeId;
      break;
    case NodeType::Conditional:
      nodeId =
          node.conditionCallback ? node.conditionCallback() : node.nextNodeId;
      break;
    case NodeType::End:
      nodeId = -1;
      break;
    }
  }
  throw std::runtime_error(
      "Script exceeded 1024 automatic transitions (possible cycle)");
}

void VNManager::advance() {
  if (isFinishedState || awaitingChoice || inputBlocked ||
      currentNodeId == -1 || engine.isStoryPaused())
    return;
  auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
  if (box.isVisible() && !box.isDialogueComplete()) {
    box.completeDialogue();
    return;
  }
  jumpToNode(scriptNodes.at(currentNodeId).nextNodeId);
}

void VNManager::onEvent(EventRef event) {
  if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
       (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN)) ||
      (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
       event.button.button == SDL_BUTTON_LEFT))
    advance();
}

void VNManager::update() {
  if (engine.isStoryPaused() || isFinishedState || currentNodeId < 0)
    return;
  const auto &node = scriptNodes.at(currentNodeId);
  if (isTimedNode(node)) {
    const auto &timeline = engine.GetTimeline();
    awaitingChoice = GetChoiceQuestionRuntime(engine).isActive();
    if (const auto target = timeline.pendingBranch())
      jumpToNode(*target);
    else if (!timeline.isPlaying())
      jumpToNode(node.nextNodeId);
  }
}

VNSave VNManager::captureSave(const std::string &data) const {
  if (isFinishedState || currentNodeId < 0)
    throw std::logic_error("Cannot save without an active script node");
  VNSave state;
  state.nodeId = currentNodeId;
  state.mode = getMode();
  state.gameData = data;
  if (state.mode == ScriptMode::Timed) {
    const auto &timeline = engine.GetTimeline();
    state.timelinePosition = timeline.seconds();
    state.resolvedChoices = timeline.getResolvedChoices();
    state.pendingBranch = timeline.pendingBranch();
  }
  return state;
}

void VNManager::save(const std::string &path, const std::string &data) const {
  captureSave(data).write(path);
}

void VNManager::restoreSave(const VNSave &state) {
  const auto found = scriptNodes.find(state.nodeId);
  if (found == scriptNodes.end())
    throw std::invalid_argument("Save refers to a node outside this script");
  const bool timed = isTimedNode(found->second);
  if ((state.mode != ScriptMode::Timed && state.mode != ScriptMode::Standard) ||
      timed != (state.mode == ScriptMode::Timed) ||
      !std::isfinite(state.timelinePosition) || state.timelinePosition < 0)
    throw std::invalid_argument("Save mode does not match script node");
  if (state.pendingBranch && *state.pendingBranch != -1 &&
      scriptNodes.count(*state.pendingBranch) == 0)
    throw std::invalid_argument("Unknown saved branch target");
  if (timed) {
    const auto script = timedScript(localizedNode(state.nodeId));
    // play validates the snapshot before replacing the running timeline.
    engine.GetTimeline().play(script, state.timelinePosition,
                              state.resolvedChoices, state.pendingBranch);
    engine.GetTimeline().setPaused(engine.isStoryPaused());
    currentNodeId = state.nodeId;
    isFinishedState = false;
    inputBlocked = true;
    awaitingChoice = GetChoiceQuestionRuntime(engine).isActive();
  } else {
    if (state.timelinePosition != 0 || !state.resolvedChoices.empty() ||
        state.pendingBranch)
      throw std::invalid_argument(
          "Unexpected timeline data in a standard save");
    jumpToNode(state.nodeId);
  }
}

ScriptNode &VNManager::getScriptNode(int nodeId) {
  return scriptNodes.at(nodeId);
}

const ScriptNode &VNManager::getScriptNode(int nodeId) const {
  return scriptNodes.at(nodeId);
}

std::vector<int> VNManager::getNodeIds() const {
  std::vector<int> ids;
  ids.reserve(scriptNodes.size());
  for (const auto &node : scriptNodes)
    ids.push_back(node.first);
  std::sort(ids.begin(), ids.end());
  return ids;
}

void VNManager::dumpTranslation(const std::string &path) const {
  if (scriptNodes.empty())
    throw std::logic_error("Load a script before exporting translations");
  engine.GetTranslation().dump(path, translationSources());
}

Translation::SourceTexts VNManager::translationSources() const {
  Translation::SourceTexts texts;
  for (int id : getNodeIds()) {
    const auto &node = scriptNodes.at(id);
    auto &source = texts[id];
    source = {node.speaker, node.text, node.choiceQuestion};
    for (const auto &choice : node.choices)
      source.push_back(choice.label);
    for (const auto &cue : node.timeline.cues) {
      if (cue.type == CueType::Audio || cue.type == CueType::Video)
        continue;
      source.push_back(cue.speaker);
      source.push_back(cue.value);
      for (const auto &choice : cue.choices)
        source.push_back(choice.label);
    }
  }
  return texts;
}

void VNManager::debugJumpToNode(int nodeId) {
  if (engine.GetSettingsMenu().isOpen())
    throw std::logic_error(
        "Close engine settings before jumping to a script node");
  if (nodeId != -1 && scriptNodes.count(nodeId) == 0)
    throw std::invalid_argument("Unknown debug node ID");
  auto &runtime = engine.GetRuntime();
  if (runtime.GetState() != GameState::Gameplay)
    throw std::logic_error("Start gameplay before jumping to a script node");
  const auto scenes = runtime.GetActiveScenes();
  for (auto *scene : scenes)
    if (scene && scene->capturesInput() &&
        scene != &GetChoiceQuestionRuntime(engine) &&
        scene != &GetVNBacklogRuntime(engine) &&
        scene != &GetInGameMenuRuntime(engine))
      runtime.RemoveScene(scene);
  if (SDL_TextInputActive(&engine.GetWindow()) &&
      !SDL_StopTextInput(&engine.GetWindow()))
    throw std::runtime_error(SDL_GetError());
  GetVNBacklogRuntime(engine).close();
  GetInGameMenuRuntime(engine).close();
  jumpToNode(nodeId);
}

void VNManager::loadKtrf(const std::string &path) {
  std::vector<ScriptNode> bindings;
  for (int id : getNodeIds())
    bindings.push_back(scriptNodes.at(id));
  auto document = KtrfScript::load(path, bindings);
  loadScript(document.nodes, document.mode);
  startScript(document.entryNodeId);
}

void VNManager::dumpKtrf(const std::string &path) const {
  KtrfScript document;
  document.mode = masterMode;
  for (int id : getNodeIds())
    document.nodes.push_back(scriptNodes.at(id));
  document.entryNodeId = currentNodeId >= 0 ? currentNodeId :
      (document.nodes.empty() ? -1 : document.nodes.front().id);
  document.dump(path);
}

VNManager &GetVNManagerRuntime(Engine &engineRef) {
  static VNManager manager{engineRef};
  return manager;
}

} // namespace Kotonoha
