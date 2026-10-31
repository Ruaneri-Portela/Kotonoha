#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/script/KtrfScript.hpp>

#include <json-c/json.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace Kotonoha {
namespace {
using JSON = std::unique_ptr<json_object, decltype(&json_object_put)>;
JSON object() { return {json_object_new_object(), json_object_put}; }
json_object *field(json_object *value, const char *key) {
  json_object *result = nullptr;
  if (!json_object_is_type(value, json_type_object) ||
      !json_object_object_get_ex(value, key, &result))
    throw std::invalid_argument(std::string("KTRF: missing field ") + key);
  return result;
}
void put(json_object *value, const char *key, const std::string &text) {
  json_object_object_add(
      value, key,
      json_object_new_string_len(text.data(), static_cast<int>(text.size())));
}
void put(json_object *value, const char *key, int number) {
  json_object_object_add(value, key, json_object_new_int(number));
}
void put(json_object *value, const char *key, double number) {
  json_object_object_add(value, key, json_object_new_double(number));
}
std::string text(json_object *value) {
  if (!json_object_is_type(value, json_type_string))
    throw std::invalid_argument("KTRF: expected a string");
  std::string result(json_object_get_string(value),
                     json_object_get_string_len(value));
  if (result.find('\0') != std::string::npos)
    throw std::invalid_argument("KTRF: embedded NUL in string");
  return result;
}
int integer(json_object *value) {
  if (!json_object_is_type(value, json_type_int))
    throw std::invalid_argument("KTRF: expected an integer");
  const auto number = json_object_get_int64(value);
  if (number < std::numeric_limits<int>::min() ||
      number > std::numeric_limits<int>::max())
    throw std::invalid_argument("KTRF: integer out of range");
  return static_cast<int>(number);
}
double number(json_object *value) {
  if (!json_object_is_type(value, json_type_int) &&
      !json_object_is_type(value, json_type_double))
    throw std::invalid_argument("KTRF: expected a number");
  const double result = json_object_get_double(value);
  if (!std::isfinite(result))
    throw std::invalid_argument("KTRF: nonfinite number");
  return result;
}
json_object *array(json_object *value) {
  if (!json_object_is_type(value, json_type_array))
    throw std::invalid_argument("KTRF: expected an array");
  return value;
}
template <typename Enum> Enum enumeration(json_object *value, int last) {
  const auto result = integer(value);
  if (result < 0 || result > last)
    throw std::invalid_argument("KTRF: unknown enumeration value");
  return static_cast<Enum>(result);
}
template <typename Choice>
json_object *choicesJSON(const std::vector<Choice> &choices) {
  auto *result = json_object_new_array();
  for (const auto &choice : choices) {
    auto entry = object();
    put(entry.get(), "label", choice.label);
    put(entry.get(), "target", choice.targetNodeId);
    json_object_array_add(result, entry.release());
  }
  return result;
}
template <typename Choice> std::vector<Choice> readChoices(json_object *value) {
  std::vector<Choice> result;
  array(value);
  for (size_t i = 0; i < json_object_array_length(value); ++i) {
    auto *entry = json_object_array_get_idx(value, i);
    result.push_back(
        {text(field(entry, "label")), integer(field(entry, "target"))});
  }
  return result;
}
void timeoutJSON(json_object *value, ChoiceTimeout policy, int target) {
  put(value, "timeout", static_cast<int>(policy));
  put(value, "timeout_target", target);
}
struct Document {
  Kotonoha_KtrfDocument value{};
  ~Document() { Kotonoha_KtrfClean(&value); }
};
} // namespace

void KtrfScript::validate() const {
  if (mode != ScriptMode::Standard && mode != ScriptMode::Timed)
    throw std::invalid_argument("KTRF: unknown script mode");
  if (nodes.empty())
    throw std::invalid_argument("KTRF: empty script");
  std::set<int> ids;
  for (const auto &node : nodes) {
    if (node.id < 0 || !ids.insert(node.id).second)
      throw std::invalid_argument("KTRF: duplicate or negative node ID");
    if (node.type < NodeType::Dialogue || node.type > NodeType::End)
      throw std::invalid_argument("KTRF: unknown node type");
    if (!std::isfinite(node.duration) || node.duration < 0)
      throw std::invalid_argument("KTRF: invalid node duration");
    if (node.timeout < ChoiceTimeout::Pause ||
        node.timeout > ChoiceTimeout::Select)
      throw std::invalid_argument("KTRF: invalid timeout policy");
    if (node.type == NodeType::Conditional && !node.conditionCallback)
      throw std::invalid_argument(
          "KTRF: conditional requires a registered native callback");
    if (node.type == NodeType::Choice && node.choices.empty())
      throw std::invalid_argument("KTRF: choice requires options");
    if (node.type == NodeType::Timeline)
      node.timeline.validate();
    else if (mode == ScriptMode::Timed && (node.type == NodeType::Dialogue ||
                                           node.type == NodeType::Choice)) {
      TimelineCue cue;
      cue.type =
          node.type == NodeType::Choice ? CueType::Choice : CueType::Dialogue;
      cue.duration = node.duration;
      cue.timeout = node.timeout;
      cue.timeoutTargetNodeId = node.timeoutTargetNodeId;
      for (const auto &option : node.choices)
        cue.choices.push_back({option.label, option.targetNodeId});
      TimelineScript{node.duration, {cue}}.validate();
    }
  }
  auto target = [&](int id) {
    if (id != -1 && !ids.count(id))
      throw std::invalid_argument("KTRF: unknown target " + std::to_string(id));
  };
  if (!ids.count(entryNodeId))
    throw std::invalid_argument("KTRF: unknown entry node");
  for (const auto &node : nodes) {
    target(node.nextNodeId);
    for (const auto &choice : node.choices)
      target(choice.targetNodeId);
    if (node.timeout == ChoiceTimeout::Select)
      target(node.timeoutTargetNodeId);
    for (const auto &cue : node.timeline.cues) {
      for (const auto &choice : cue.choices)
        target(choice.targetNodeId);
      if (cue.timeout == ChoiceTimeout::Select)
        target(cue.timeoutTargetNodeId);
    }
  }
}

void KtrfScript::dump(const std::string &path) const {
  validate();
  auto root = object();
  put(root.get(), "profile", std::string("kotonoha.vn"));
  put(root.get(), "version", 1);
  put(root.get(), "mode", static_cast<int>(mode));
  put(root.get(), "entry", entryNodeId);
  auto *list = json_object_new_array();
  json_object_object_add(root.get(), "nodes", list);
  auto sorted = nodes;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  for (const auto &node : sorted) {
    auto entry = object();
    put(entry.get(), "id", node.id);
    put(entry.get(), "type", static_cast<int>(node.type));
    put(entry.get(), "name", node.name);
    put(entry.get(), "speaker", node.speaker);
    put(entry.get(), "text", node.text);
    put(entry.get(), "background", node.backgroundAsset);
    put(entry.get(), "character", node.characterAsset);
    put(entry.get(), "question", node.choiceQuestion);
    put(entry.get(), "next", node.nextNodeId);
    put(entry.get(), "duration", node.duration);
    put(entry.get(), "native_callback",
        node.actionCallback || node.conditionCallback ? 1 : 0);
    timeoutJSON(entry.get(), node.timeout, node.timeoutTargetNodeId);
    json_object_object_add(entry.get(), "choices", choicesJSON(node.choices));
    put(entry.get(), "timeline_duration", node.timeline.duration);
    auto *cues = json_object_new_array();
    json_object_object_add(entry.get(), "cues", cues);
    for (const auto &cue : node.timeline.cues) {
      auto item = object();
      put(item.get(), "at", cue.at);
      put(item.get(), "duration", cue.duration);
      put(item.get(), "type", static_cast<int>(cue.type));
      put(item.get(), "value", cue.value);
      put(item.get(), "speaker", cue.speaker);
      put(item.get(), "channel", cue.channelName);
      timeoutJSON(item.get(), cue.timeout, cue.timeoutTargetNodeId);
      json_object_object_add(item.get(), "choices", choicesJSON(cue.choices));
      json_object_array_add(cues, item.release());
    }
    json_object_array_add(list, entry.release());
  }
  const std::string payload = json_object_to_json_string_ext(
      root.get(), JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
  if (std::filesystem::path(path).extension() == ".json") {
    writeTextFileAtomically(path, payload + "\n");
    return;
  }
  // STRS reserves index zero for the empty string, as in the original
  // container.
  const uint8_t strings[16]{1};
  Kotonoha_KtrfSection sections[2]{};
  std::copy_n("STRS", 4, sections[0].type);
  sections[0].flags = KOTONOHA_KTRF_SECTION_REQUIRED;
  sections[0].payload = strings;
  sections[0].stored_size = sizeof(strings);
  sections[0].item_count = 1;
  std::copy_n("VNSC", 4, sections[1].type);
  sections[1].payload = reinterpret_cast<const uint8_t *>(payload.data());
  sections[1].stored_size = payload.size();
  sections[1].item_count = static_cast<uint32_t>(nodes.size());
  Document document;
  Kotonoha_KtrfError error{};
  if (!Kotonoha_KtrfBuild(sections, 2, &document.value, &error))
    throw std::runtime_error(std::string("Cannot encode KTRF: ") +
                             error.message);
  writeTextFileAtomically(
      path, std::string(reinterpret_cast<const char *>(document.value.bytes),
                        document.value.size));
}

KtrfScript KtrfScript::load(const std::string &path,
                            const std::vector<ScriptNode> &nativeBindings) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open KTRF: " + path);
  std::string source((std::istreambuf_iterator<char>(input)), {});
  if (input.bad())
    throw std::runtime_error("Cannot read KTRF: " + path);
  Document document;
  const Kotonoha_KtrfSection *section = nullptr;
  if (source.compare(0, 4, "KTRF") == 0) {
    Kotonoha_KtrfError error{};
    if (!Kotonoha_KtrfLoadMemoryCopy(source.data(), source.size(),
                                     &document.value, &error))
      throw std::runtime_error(std::string("Invalid KTRF: ") + error.message);
    section = Kotonoha_KtrfFindSection(&document.value, "VNSC");
    if (!section)
      throw std::invalid_argument(
          "KTRF requires its game profile (not kotonoha.vn)");
    source.assign(reinterpret_cast<const char *>(section->payload),
                  section->stored_size);
  }
  if (source.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("KTRF JSON exceeds parser limit");
  std::unique_ptr<json_tokener, decltype(&json_tokener_free)> parser(
      json_tokener_new(), json_tokener_free);
  if (!parser)
    throw std::bad_alloc();
  json_tokener_set_flags(parser.get(),
                         JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  JSON root(json_tokener_parse_ex(parser.get(), source.c_str(),
                                  static_cast<int>(source.size() + 1)),
            json_object_put);
  if (json_tokener_get_error(parser.get()) != json_tokener_success ||
      source.find_first_not_of(" \r\n\t",
                               json_tokener_get_parse_end(parser.get())) !=
          std::string::npos)
    throw std::invalid_argument("KTRF: invalid JSON");
  if (text(field(root.get(), "profile")) != "kotonoha.vn" ||
      integer(field(root.get(), "version")) != 1)
    throw std::invalid_argument("KTRF: unsupported script profile/version");
  KtrfScript result;
  result.mode = enumeration<ScriptMode>(field(root.get(), "mode"), 1);
  result.entryNodeId = integer(field(root.get(), "entry"));
  auto *list = array(field(root.get(), "nodes"));
  if (section && section->item_count != json_object_array_length(list))
    throw std::invalid_argument("KTRF: VNSC node count mismatch");
  for (size_t i = 0; i < json_object_array_length(list); ++i) {
    auto *entry = json_object_array_get_idx(list, i);
    ScriptNode node;
    node.id = integer(field(entry, "id"));
    node.type = enumeration<NodeType>(field(entry, "type"), 5);
    node.name = text(field(entry, "name"));
    node.speaker = text(field(entry, "speaker"));
    node.text = text(field(entry, "text"));
    node.backgroundAsset = text(field(entry, "background"));
    node.characterAsset = text(field(entry, "character"));
    node.choiceQuestion = text(field(entry, "question"));
    node.nextNodeId = integer(field(entry, "next"));
    node.duration = number(field(entry, "duration"));
    node.timeout = enumeration<ChoiceTimeout>(field(entry, "timeout"), 2);
    node.timeoutTargetNodeId = integer(field(entry, "timeout_target"));
    node.choices = readChoices<ChoiceOptionData>(field(entry, "choices"));
    node.timeline.duration = number(field(entry, "timeline_duration"));
    auto *cues = array(field(entry, "cues"));
    for (size_t j = 0; j < json_object_array_length(cues); ++j) {
      auto *item = json_object_array_get_idx(cues, j);
      TimelineCue cue;
      cue.at = number(field(item, "at"));
      cue.duration = number(field(item, "duration"));
      cue.type = enumeration<CueType>(field(item, "type"), 4);
      cue.value = text(field(item, "value"));
      cue.speaker = text(field(item, "speaker"));
      cue.channelName = text(field(item, "channel"));
      cue.timeout = enumeration<ChoiceTimeout>(field(item, "timeout"), 2);
      cue.timeoutTargetNodeId = integer(field(item, "timeout_target"));
      cue.choices = readChoices<TimedChoiceOption>(field(item, "choices"));
      node.timeline.cues.push_back(std::move(cue));
    }
    const int native = integer(field(entry, "native_callback"));
    if (native != 0 && native != 1)
      throw std::invalid_argument("KTRF: invalid native callback flag");
    if (native) {
      const auto binding = std::find_if(
          nativeBindings.begin(), nativeBindings.end(),
          [&](const auto &candidate) {
            return candidate.id == node.id && candidate.type == node.type;
          });
      if (binding == nativeBindings.end() ||
          (!binding->actionCallback && !binding->conditionCallback))
        throw std::invalid_argument(
            "KTRF: unregistered native callback at node " +
            std::to_string(node.id));
      node.actionCallback = binding->actionCallback;
      node.conditionCallback = binding->conditionCallback;
    }
    result.nodes.push_back(std::move(node));
  }
  result.validate();
  return result;
}
} // namespace Kotonoha
