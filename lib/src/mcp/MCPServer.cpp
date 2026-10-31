#include <Kotonoha/mcp/MCPServer.hpp>

#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <json-c/json.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <csignal>
#include <poll.h>
#include <unistd.h>
#endif

namespace Kotonoha {
namespace {
struct JSONDeleter {
  void operator()(json_object *value) const { json_object_put(value); }
};
using JSON = std::unique_ptr<json_object, JSONDeleter>;

JSON object() { return JSON{json_object_new_object()}; }
json_object *field(json_object *value, const char *name) {
  json_object *result = nullptr;
  if (value && json_object_is_type(value, json_type_object))
    json_object_object_get_ex(value, name, &result);
  return result;
}
void put(json_object *value, const char *name, const std::string &text) {
  json_object_object_add(
      value, name,
      json_object_new_string_len(text.data(), static_cast<int>(text.size())));
}
void put(json_object *value, const char *name, bool flag) {
  json_object_object_add(value, name, json_object_new_boolean(flag));
}
void put(json_object *value, const char *name, int number) {
  json_object_object_add(value, name, json_object_new_int(number));
}
void put(json_object *value, const char *name, double number) {
  json_object_object_add(value, name, json_object_new_double(number));
}
std::string stringValue(json_object *value) {
  if (!value || !json_object_is_type(value, json_type_string))
    throw std::invalid_argument("Expected a JSON string");
  std::string text(json_object_get_string(value),
                   json_object_get_string_len(value));
  if (text.find('\0') != std::string::npos)
    throw std::invalid_argument("Embedded NUL characters are not supported");
  return text;
}
int integer(json_object *value) {
  if (!value || !json_object_is_type(value, json_type_int))
    throw std::invalid_argument("Expected an integer");
  const auto number = json_object_get_int64(value);
  if (number < std::numeric_limits<int>::min() ||
      number > std::numeric_limits<int>::max())
    throw std::invalid_argument("Integer is out of range");
  return static_cast<int>(number);
}
bool boolean(json_object *value) {
  if (!value || !json_object_is_type(value, json_type_boolean))
    throw std::invalid_argument("Expected a boolean");
  return json_object_get_boolean(value);
}

struct Tool {
  const char *description;
  const char *schema;
};

const std::map<std::string, Tool> &tools() {
  static const std::map<std::string, Tool> definitions{
      {"engine_state",
       {"Inspect game, node, clock, dialogue and visible choices.",
        R"({"type":"object","properties":{},"additionalProperties":false})"}},
      {"start_game",
       {"Start a new game and load its story.",
        R"({"type":"object","properties":{},"additionalProperties":false})"}},
      {"list_nodes",
       {"List loaded nodes with names, types, text and choices.",
        R"({"type":"object","properties":{},"additionalProperties":false})"}},
      {"jump_node",
       {"Debug jump to a loaded node; prior game actions are not replayed.",
        R"({"type":"object","properties":{"node_id":{"type":"integer","minimum":-1}},"required":["node_id"],"additionalProperties":false})"}},
      {"jump_runtime_node",
       {"Jump to an engine or game-specific node by its listed string ID.",
        R"({"type":"object","properties":{"node_id":{"type":"string","minLength":1}},"required":["node_id"],"additionalProperties":false})"}},
      {"load_ktrf",
       {"Replace the current graph with a compatible KTRF within the MCP file root.",
        R"({"type":"object","properties":{"path":{"type":"string","minLength":1}},"required":["path"],"additionalProperties":false})"}},
      {"dump_ktrf",
       {"Dump the current KTRF within the MCP file root (.json is editable for the VN profile).",
        R"({"type":"object","properties":{"path":{"type":"string","minLength":1}},"required":["path"],"additionalProperties":false})"}},
      {"advance",
       {"Complete/advance a traditional dialogue; rejects blocked input.",
        R"({"type":"object","properties":{},"additionalProperties":false})"}},
      {"choose",
       {"Answer a visible question using its zero-based option index.",
        R"({"type":"object","properties":{"index":{"type":"integer","minimum":0}},"required":["index"],"additionalProperties":false})"}},
      {"submit_text",
       {"Enter and confirm text in the active game text-input modal.",
        R"({"type":"object","properties":{"text":{"type":"string","maxLength":255}},"required":["text"],"additionalProperties":false})"}},
      {"set_paused",
       {"Pause/resume agent control. Game modals and unanswered deadlines "
        "still pause independently.",
        R"({"type":"object","properties":{"paused":{"type":"boolean"}},"required":["paused"],"additionalProperties":false})"}},
      {"advance_time",
       {"Step an active timeline by 0-60 seconds while agent-paused, with no "
        "game/debug modal.",
        R"({"type":"object","properties":{"seconds":{"type":"number","minimum":0,"maximum":60}},"required":["seconds"],"additionalProperties":false})"}},
      {"set_debug",
       {"Set inspector, FPS and node/clock visibility without synthesizing "
        "keys.",
        R"({"type":"object","properties":{"visible":{"type":"boolean"},"fps":{"type":"boolean"},"node_clock":{"type":"boolean"}},"additionalProperties":false})"}},
      {"save_game",
       {"Save the game through its runtime, including inventory, within the "
        "MCP file root.",
        R"({"type":"object","properties":{"path":{"type":"string","minLength":1}},"required":["path"],"additionalProperties":false})"}},
      {"load_game",
       {"Restore the game through its runtime from a file within the MCP file "
        "root.",
        R"({"type":"object","properties":{"path":{"type":"string","minLength":1}},"required":["path"],"additionalProperties":false})"}},
      {"dump_translation",
       {"Export all loaded node texts as translation.json within the MCP file "
        "root.",
        R"({"type":"object","properties":{"path":{"type":"string","minLength":1}},"required":["path"],"additionalProperties":false})"}},
  };
  return definitions;
}

JSON parse(const std::string &line) {
  std::unique_ptr<json_tokener, decltype(&json_tokener_free)> parser(
      json_tokener_new(), json_tokener_free);
  if (!parser)
    throw std::bad_alloc();
  json_tokener_set_flags(parser.get(),
                         JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  JSON value{json_tokener_parse_ex(parser.get(), line.c_str(),
                                   static_cast<int>(line.size() + 1))};
  if (json_tokener_get_error(parser.get()) != json_tokener_success)
    throw std::invalid_argument("Invalid UTF-8 JSON");
  const auto consumed = json_tokener_get_parse_end(parser.get());
  if (consumed < line.size() &&
      line.find_first_not_of(" \r\n\t", consumed) != std::string::npos)
    throw std::invalid_argument("Unexpected content after JSON message");
  return value;
}

void validateArguments(json_object *arguments, const Tool &tool) {
  if (!arguments || !json_object_is_type(arguments, json_type_object))
    throw std::invalid_argument("Tool arguments must be an object");
  const auto schema = parse(tool.schema);
  auto *properties = field(schema.get(), "properties");
  json_object_object_foreach(arguments, key, value) {
    auto *property = field(properties, key);
    if (!property)
      throw std::invalid_argument(std::string("Unknown argument: ") + key);
    const auto type = stringValue(field(property, "type"));
    if (type == "string") {
      const auto text = stringValue(value);
      if (auto *minimum = field(property, "minLength");
          minimum && text.size() < static_cast<std::size_t>(integer(minimum)))
        throw std::invalid_argument("String argument is too short");
      if (auto *maximum = field(property, "maxLength");
          maximum && text.size() > static_cast<std::size_t>(integer(maximum)))
        throw std::invalid_argument("String argument exceeds byte limit");
    } else if (type == "integer") {
      integer(value);
    } else if (type == "boolean") {
      boolean(value);
    } else if (!json_object_is_type(value, json_type_double) &&
               !json_object_is_type(value, json_type_int)) {
      throw std::invalid_argument("Expected a number");
    }
    if (auto *minimum = field(property, "minimum");
        minimum &&
        json_object_get_double(value) < json_object_get_double(minimum))
      throw std::invalid_argument("Argument below minimum");
    if (auto *maximum = field(property, "maximum");
        maximum &&
        json_object_get_double(value) > json_object_get_double(maximum))
      throw std::invalid_argument("Argument above maximum");
  }
  auto *required = field(schema.get(), "required");
  if (required)
    for (std::size_t i = 0; i < json_object_array_length(required); ++i) {
      const auto key = stringValue(json_object_array_get_idx(required, i));
      if (!field(arguments, key.c_str()))
        throw std::invalid_argument("Missing required argument: " + key);
    }
}
} // namespace

struct MCPServer::State {
  Engine &engine;
  std::filesystem::path root;
  std::string buffer;
  bool initialized = false, ready = false, eof = false;
#ifndef _WIN32
  using SignalHandler = void (*)(int);
  SignalHandler previousPipeHandler;
#endif

  State(Engine &engine, const std::string &fileRoot)
      : engine(engine), root(std::filesystem::canonical(fileRoot)) {
    if (!std::filesystem::is_directory(root))
      throw std::invalid_argument("MCP file root must be a directory");
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) < 0 ||
        _setmode(_fileno(stdout), _O_BINARY) < 0)
      throw std::runtime_error("Cannot configure MCP binary stdio");
#else
    previousPipeHandler = std::signal(SIGPIPE, SIG_IGN);
    if (previousPipeHandler == SIG_ERR)
      throw std::runtime_error("Cannot configure MCP pipe handling");
#endif
  }

  ~State() {
#ifndef _WIN32
    std::signal(SIGPIPE, previousPipeHandler);
#endif
  }

  std::string filePath(json_object *arguments) const {
    const auto requested = stringValue(field(arguments, "path"));
    if (requested.empty())
      throw std::invalid_argument("Path cannot be empty");
    const auto path = std::filesystem::weakly_canonical(root / requested);
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative == "." || relative.is_absolute() ||
        *relative.begin() == "..")
      throw std::invalid_argument(
          "File path must stay inside the MCP file root");
    return path.string();
  }

  void send(json_object *message) {
    const std::string line = std::string(json_object_to_json_string_ext(
                                 message, JSON_C_TO_STRING_PLAIN)) +
                             "\n";
    if (std::fwrite(line.data(), 1, line.size(), stdout) != line.size() ||
        std::fflush(stdout) != 0)
      throw std::runtime_error("Cannot write MCP stdout response");
  }

  void reply(json_object *id, JSON result, int error = 0,
             const std::string &message = "") {
    auto response = object();
    put(response.get(), "jsonrpc", std::string("2.0"));
    json_object_object_add(response.get(), "id", json_object_get(id));
    if (error) {
      auto details = object();
      put(details.get(), "code", error);
      put(details.get(), "message", message);
      json_object_object_add(response.get(), "error", details.release());
    } else {
      json_object_object_add(response.get(), "result", result.release());
    }
    send(response.get());
  }

  JSON snapshot() const {
    auto result = object();
    const auto state = engine.GetRuntime().GetState();
    put(result.get(), "game_state",
        std::string(state == GameState::Gameplay ? "gameplay"
                    : state == GameState::Menu   ? "menu"
                                                 : "splash"));
    const auto &manager = GetVNManagerRuntime(engine);
    const auto &timeline = engine.GetTimeline();
    const auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
    const auto &choice = GetChoiceQuestionRuntime(engine);
    const auto &scenes = engine.GetRuntime().GetActiveScenes();
    const bool gameplay = state == GameState::Gameplay;
    const bool boxVisible =
        box.isVisible() &&
        std::find(scenes.begin(), scenes.end(), &box) != scenes.end();
    const bool choiceVisible =
        choice.isActive() &&
        std::find(scenes.begin(), scenes.end(), &choice) != scenes.end();
    put(result.get(), "node_id", gameplay ? manager.getCurrentNodeId() : -1);
    put(result.get(), "node_name",
        gameplay ? engine.GetRuntime().debugNode() : std::string());
    put(result.get(), "mode",
        std::string(manager.getMode() == ScriptMode::Timed ? "timed"
                                                           : "standard"));
    put(result.get(), "paused", engine.isStoryPaused() || timeline.isPaused());
    put(result.get(), "timeline_playing", timeline.isPlaying());
    put(result.get(), "seconds",
        engine.GetRuntime().debugNodeSeconds().value_or(timeline.seconds()));
    put(result.get(), "master_volume", double(engine.GetAudio().getMasterVolume()));
    put(result.get(), "master_muted", engine.GetAudio().isMasterMuted());
    put(result.get(), "debug_ui_scale", double(engine.GetSettings().debugUIScale));
    put(result.get(), "waiting_for_choice", choiceVisible);
    bool textInput = false;
    for (auto *scene : engine.GetRuntime().GetActiveScenes())
      textInput =
          textInput || dynamic_cast<TextInputDialog *>(scene) != nullptr;
    put(result.get(), "waiting_for_text", textInput);
    put(result.get(), "textbox_visible", boxVisible);
    put(result.get(), "dialogue", boxVisible ? box.getText() : "");
    put(result.get(), "speaker", boxVisible ? box.getSpeaker() : "");
    put(result.get(), "subtitle", timeline.activeSubtitleText());
    put(result.get(), "question", choiceVisible ? choice.getQuestion() : "");
    put(result.get(), "debug_visible", engine.GetDebug().isVisible());
    put(result.get(), "settings_visible", engine.GetSettingsMenu().isOpen());
    put(result.get(), "show_fps", engine.GetDebug().getSettings().showFPS);
    put(result.get(), "show_node_clock",
        engine.GetDebug().getSettings().showNodeClock);
    auto *options = json_object_new_array();
    if (choiceVisible) {
      int index = 0;
      for (const auto &option : choice.getOptions()) {
        auto item = object();
        put(item.get(), "index", index++);
        put(item.get(), "label", option.label);
        put(item.get(), "target_node_id", option.optionId);
        json_object_array_add(options, item.release());
      }
    }
    json_object_object_add(result.get(), "choices", options);
    return result;
  }

  JSON call(const std::string &name, json_object *args) {
    auto &runtime = engine.GetRuntime();
    auto &manager = GetVNManagerRuntime(engine);
    if (name == "engine_state")
      return snapshot();
    if (name == "list_nodes") {
      auto result = object();
      auto *nodes = json_object_new_array();
      const auto custom = runtime.debugNodes();
      for (const auto &node : custom) {
        auto item = object();
        put(item.get(), "id", node.id);
        put(item.get(), "name", node.label);
        auto *assets = json_object_new_array();
        for (const auto &asset : node.assets)
          json_object_array_add(assets, json_object_new_string(asset.c_str()));
        json_object_object_add(item.get(), "assets", assets);
        json_object_array_add(nodes, item.release());
      }
      for (int id : custom.empty() ? manager.getNodeIds() : std::vector<int>{}) {
        const auto &node = manager.getScriptNode(id);
        auto item = object();
        put(item.get(), "id", id);
        put(item.get(), "name", node.name);
        put(item.get(), "type", static_cast<int>(node.type));
        put(item.get(), "text", node.text);
        put(item.get(), "question", node.choiceQuestion);
        put(item.get(), "next_node_id", node.nextNodeId);
        auto *choices = json_object_new_array();
        for (const auto &option : node.choices) {
          auto choice = object();
          put(choice.get(), "label", option.label);
          put(choice.get(), "target_node_id", option.targetNodeId);
          json_object_array_add(choices, choice.release());
        }
        json_object_object_add(item.get(), "choices", choices);
        json_object_array_add(nodes, item.release());
      }
      json_object_object_add(result.get(), "nodes", nodes);
      return result;
    }
    if (engine.GetSettingsMenu().isOpen() && name != "set_paused" &&
        name != "set_debug" && name != "dump_translation" && name != "dump_ktrf")
      throw std::logic_error(
          "Close engine settings before controlling the story");
    if (name == "set_paused") {
      engine.setAgentPaused(boolean(field(args, "paused")));
    } else if (name == "set_debug") {
      auto &debug = engine.GetDebug();
      if (auto *value = field(args, "visible"))
        debug.setVisible(boolean(value));
      if (auto *value = field(args, "fps"))
        debug.getSettings().showFPS = boolean(value);
      if (auto *value = field(args, "node_clock"))
        debug.getSettings().showNodeClock = boolean(value);
      engine.GetTimeline().setPaused(engine.isStoryPaused());
    } else if (name == "start_game") {
      runtime.SetState(GameState::Gameplay);
      runtime.init(engine);
      engine.GetTimeline().setPaused(engine.isStoryPaused());
    } else if (name == "load_game") {
      runtime.loadGame(filePath(args));
    } else if (name == "dump_translation") {
      engine.dumpTranslation(filePath(args));
    } else if (name == "dump_ktrf") {
      engine.dumpKtrf(filePath(args));
    } else if (name == "load_ktrf") {
      engine.loadKtrf(filePath(args));
    } else {
      if (runtime.GetState() != GameState::Gameplay)
        throw std::logic_error("Start gameplay before controlling the story");
      if (name == "jump_node") {
        engine.debugJumpToNode(std::to_string(integer(field(args, "node_id"))));
      } else if (name == "jump_runtime_node") {
        engine.debugJumpToNode(stringValue(field(args, "node_id")));
      } else if (name == "save_game") {
        runtime.saveGame(filePath(args));
      } else if (name == "advance_time") {
        engine.advanceTimeline(json_object_get_double(field(args, "seconds")));
      } else if (name == "advance") {
        if (manager.isFinished() || manager.isInputBlocked() ||
            engine.isStoryPaused())
          throw std::logic_error("Manual advance is blocked");
        manager.advance();
      } else if (name == "choose") {
        if (runtime.isStoryPaused() || engine.GetDebug().isVisible())
          throw std::logic_error("Close game/debug modals before choosing");
        GetChoiceQuestionRuntime(engine).chooseOption(
            static_cast<std::size_t>(integer(field(args, "index"))));
        if (const auto target = engine.GetTimeline().pendingBranch())
          manager.jumpToNode(*target);
      } else if (name == "submit_text") {
        const auto &scenes = runtime.GetActiveScenes();
        TextInputDialog *dialog = nullptr;
        for (auto *scene : scenes)
          if (auto *input = dynamic_cast<TextInputDialog *>(scene))
            dialog = input;
        if (!dialog || engine.GetDebug().isVisible())
          throw std::logic_error("No game text-input modal is available");
        const auto text = stringValue(field(args, "text"));
        if (text.size() > 255 ||
            text.find_first_not_of(" \r\n\t") == std::string::npos)
          throw std::invalid_argument(
              "Text must contain 1-255 nonblank UTF-8 bytes");
        dialog->submitText(text);
        const auto &remaining = runtime.GetActiveScenes();
        if (std::any_of(remaining.begin(), remaining.end(), [](Scene *scene) {
              return dynamic_cast<TextInputDialog *>(scene) != nullptr;
            }))
          throw std::logic_error("Game did not accept the submitted text");
      }
    }
    return snapshot();
  }

  void dispatch(const std::string &line) {
    JSON request;
    try {
      request = parse(line);
    } catch (const std::invalid_argument &error) {
      reply(nullptr, {}, -32700, error.what());
      return;
    }
    json_object *id = nullptr;
    const bool hasId = request &&
                       json_object_is_type(request.get(), json_type_object) &&
                       json_object_object_get_ex(request.get(), "id", &id);
    auto *methodValue = field(request.get(), "method");
    if (!request || !json_object_is_type(request.get(), json_type_object) ||
        !field(request.get(), "jsonrpc") ||
        !json_object_is_type(field(request.get(), "jsonrpc"),
                             json_type_string) ||
        json_object_get_string_len(field(request.get(), "jsonrpc")) != 3 ||
        std::strcmp(json_object_get_string(field(request.get(), "jsonrpc")),
                    "2.0") != 0 ||
        !methodValue || !json_object_is_type(methodValue, json_type_string) ||
        (hasId && (!id || (!json_object_is_type(id, json_type_int) &&
                           !json_object_is_type(id, json_type_string))))) {
      reply(nullptr, {}, -32600, "Invalid JSON-RPC request");
      return;
    }
    const std::string method(json_object_get_string(methodValue),
                             json_object_get_string_len(methodValue));
    if (method.find('\0') != std::string::npos) {
      reply(id, {}, -32600, "Invalid method name");
      return;
    }
    if (!id) {
      if (method == "notifications/initialized" && initialized)
        ready = true;
      return;
    }
    auto *params = field(request.get(), "params");
    try {
      if (params && !json_object_is_type(params, json_type_object))
        throw std::invalid_argument("params must be an object");
      if (method == "initialize") {
        if (initialized)
          throw std::invalid_argument("MCP is already initialized");
        const auto requested = stringValue(field(params, "protocolVersion"));
        if (!field(params, "capabilities") ||
            !json_object_is_type(field(params, "capabilities"),
                                 json_type_object) ||
            !field(params, "clientInfo") ||
            !json_object_is_type(field(params, "clientInfo"), json_type_object))
          throw std::invalid_argument(
              "Missing initialize capabilities or clientInfo");
        static const std::set<std::string> versions{"2024-11-05", "2025-03-26",
                                                    "2025-06-18", "2025-11-25"};
        auto result = parse(
            R"({"protocolVersion":"2025-11-25","capabilities":{"tools":{"listChanged":false}},"serverInfo":{"name":"kotonoha-engine","version":"0.1.0"},"instructions":"Local engine control. Use start_game, then inspect state and nodes. set_paused enables deterministic advance_time. File tools are restricted to the configured MCP root."})");
        if (versions.count(requested))
          put(result.get(), "protocolVersion", requested);
        initialized = true;
        reply(id, std::move(result));
      } else if (method == "ping") {
        reply(id, object());
      } else if (!ready) {
        reply(id, {}, -32002,
              "Initialize MCP and send notifications/initialized first");
      } else if (method == "tools/list") {
        auto result = object();
        auto *list = json_object_new_array();
        for (const auto &entry : tools()) {
          auto tool = object();
          put(tool.get(), "name", entry.first);
          put(tool.get(), "description", std::string(entry.second.description));
          auto schema = parse(entry.second.schema);
          json_object_object_add(tool.get(), "inputSchema", schema.release());
          json_object_array_add(list, tool.release());
        }
        json_object_object_add(result.get(), "tools", list);
        reply(id, std::move(result));
      } else if (method == "tools/call") {
        const auto name = stringValue(field(params, "name"));
        const auto definition = tools().find(name);
        if (definition == tools().end())
          throw std::invalid_argument("Unknown tool: " + name);
        auto *args = field(params, "arguments");
        auto empty = object();
        if (!args)
          args = empty.get();
        validateArguments(args, definition->second);
        engine.synchronizeTimeline();
        auto result = object();
        auto *content = json_object_new_array();
        auto item = object();
        put(item.get(), "type", std::string("text"));
        try {
          auto data = call(name, args);
          put(item.get(), "text",
              std::string(json_object_to_json_string_ext(
                  data.get(), JSON_C_TO_STRING_PLAIN)));
          put(result.get(), "isError", false);
        } catch (const std::exception &error) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MCP %s failed: %s",
                       name.c_str(), error.what());
          put(item.get(), "text", std::string(error.what()));
          put(result.get(), "isError", true);
        }
        json_object_array_add(content, item.release());
        json_object_object_add(result.get(), "content", content);
        reply(id, std::move(result));
      } else {
        reply(id, {}, -32601, "Method not found");
      }
    } catch (const std::invalid_argument &error) {
      reply(id, {}, -32602, error.what());
    }
  }

  void readInput() {
    char chunk[4096];
    for (int count = 0; count < 16 && !eof; ++count) {
#ifdef _WIN32
      const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
      DWORD available = 0;
      if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
          eof = true;
          break;
        }
        throw std::runtime_error("MCP requires piped stdin on Windows");
      }
      if (!available)
        break;
      DWORD received = 0;
      if (!ReadFile(input, chunk, std::min<DWORD>(sizeof(chunk), available),
                    &received, nullptr))
        throw std::runtime_error("Cannot read MCP stdin");
      const auto bytes = received;
#else
      pollfd input{STDIN_FILENO, POLLIN, 0};
      const int status = poll(&input, 1, 0);
      if (status < 0) {
        if (errno == EINTR)
          break;
        throw std::runtime_error("Cannot poll MCP stdin");
      }
      if (!status)
        break;
      const auto bytes = read(STDIN_FILENO, chunk, sizeof(chunk));
      if (bytes < 0) {
        if (errno == EINTR || errno == EAGAIN)
          break;
        throw std::runtime_error("Cannot read MCP stdin");
      }
#endif
      if (bytes == 0) {
        eof = true;
        break;
      }
      buffer.append(chunk, static_cast<std::size_t>(bytes));
      if (buffer.size() > 1024 * 1024)
        throw std::runtime_error("MCP input buffer exceeds 1 MiB");
    }
  }
};

MCPServer::MCPServer(Engine &engine, const std::string &root)
    : state(std::make_unique<State>(engine, root)) {}
MCPServer::~MCPServer() = default;

bool MCPServer::pump() {
  state->readInput();
  for (int requests = 0; requests < 32; ++requests) {
    const auto end = state->buffer.find('\n');
    if (end == std::string::npos)
      break;
    const auto line = state->buffer.substr(0, end);
    state->buffer.erase(0, end + 1);
    if (!line.empty() && line != "\r")
      state->dispatch(line);
  }
  if (state->eof && state->buffer.find('\n') == std::string::npos) {
    if (!state->buffer.empty())
      state->reply(nullptr, {}, -32700, "MCP message must end with a newline");
    return false;
  }
  return true;
}

} // namespace Kotonoha
