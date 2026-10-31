#include "DemoStory.hpp"
#include <Kotonoha/componentes/ChoiceQuestionScene.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/TextInputDialog.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNSave.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

template <typename F> void rejects(F operation, const char *message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, message);
}

SDL_Event key(SDL_Keycode code) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = code;
  return event;
}

SDL_Event click(float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  return event;
}

std::uint64_t fingerprint(Kotonoha::RendererRef renderer) {
  Kotonoha::SurfacePtr pixels{SDL_RenderReadPixels(&renderer, nullptr)};
  require(static_cast<bool>(pixels), "Read back rendered frame");
  std::uint64_t hash = 14695981039346656037ull;
  for (int y = 0; y < pixels->h; y += 10)
    for (int x = 0; x < pixels->w; x += 10) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      require(SDL_ReadSurfacePixel(pixels.get(), x, y, &r, &g, &b, &a),
              "Read rendered pixel");
      hash = (hash ^ r) * 1099511628211ull;
      hash = (hash ^ g) * 1099511628211ull;
      hash = (hash ^ b) * 1099511628211ull;
    }
  return hash;
}

class TestRuntime : public Kotonoha::Runtime {
public:
  TestRuntime() : Runtime("Kotonoha tests", "1", "org.kotonoha.tests") {
    SetState(Kotonoha::GameState::Gameplay);
  }
};

struct TemporaryScript {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("kotonoha-" + std::to_string(SDL_GetPerformanceCounter()) + ".timeline");
  void write(const std::string &text) { std::ofstream(path) << text; }
  ~TemporaryScript() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

void testParser() {
  using namespace Kotonoha;
  TemporaryScript file;
  file.write("duration 5\nat 2 dialogue 2 \"Name\" \"Hello\"\n"
             "at 0 subtitle 1 \"First\" # comment\n"
             "at 0 audio 1 \"sound.wav\"\n");
  const auto script = TimelineScript::load(file.path.string());
  require(script.cues.size() == 3, "Parser cue count");
  require(script.cues[0].at == 0 && script.cues[2].speaker == "Name",
          "Stable chronological sorting");
  require(script.cues[1].value ==
              (file.path.parent_path() / "sound.wav").string(),
          "Relative media path");
  for (const char *invalid :
       {"at 0 subtitle 1 \"Missing duration\"",
        "duration 1\nat 0 subtitle 2 \"Overflow\"",
        "duration 5\nat -1 subtitle 1 \"Negative\"",
        "duration 5\nat 0 unknown 1 \"Invalid\"",
        "duration 5\nat 0 subtitle 1 unquoted",
        "duration 5\nat 0 subtitle 1 \"Unclosed",
        "duration 5\nat 0 subtitle 1 \"OK\" garbage", "duration 5\nduration 5",
        "duration 5\nat 0 audio 3 \"a\"\nat 2 audio 1 \"b\""}) {
    file.write(invalid);
    rejects([&] { TimelineScript::load(file.path.string()); },
            "Malformed timeline must fail");
  }
  rejects(
      [] {
        TimelineScript{std::numeric_limits<double>::infinity(), {}}.validate();
      },
      "Nonfinite timeline rejected");
  TimelineScript{
      2, {{0, 1, CueType::Audio, "a", ""}, {1, 1, CueType::Audio, "b", ""}}}
      .validate();
  TimelineScript{0.3, {{0.1, 0.2, CueType::Audio, "a", ""}}}.validate();
  file.write(
      "duration 3\nat 0 choice 1 \"Question?\" pause 2 \"A\" 10 \"B\" 11\n"
      "at 1 choice 1 \"Default?\" select 10 1 \"A\" 10\n"
      "at 2 choice 1 \"Continue?\" continue 1 \"A\" 10\n");
  const auto choices = TimelineScript::load(file.path.string());
  require(choices.cues[0].timeout == ChoiceTimeout::Pause &&
              choices.cues[0].choices[1].targetNodeId == 11 &&
              choices.cues[1].timeoutTargetNodeId == 10 &&
              choices.cues[2].timeout == ChoiceTimeout::Continue,
          "Timed choice syntax includes explicit timeout policy");
  for (const char *invalid :
       {"duration 1\nat 0 choice 1 \"Q\" unknown 1 \"A\" 0",
        "duration 1\nat 0 choice 1 \"Q\" pause 0",
        "duration 1\nat 0 choice 1 \"Q\" select -2 1 \"A\" 0"}) {
    file.write(invalid);
    rejects([&] { TimelineScript::load(file.path.string()); },
            "Invalid choices rejected");
  }
}

void testTranslations(Kotonoha::Engine &engine) {
  using namespace Kotonoha;
  auto &vn = GetVNManagerRuntime(engine);
  auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
  auto &choice = GetChoiceQuestionRuntime(engine);
  auto &timeline = engine.GetTimeline();
  TimelineCue timedChoice{0, 2, CueType::Choice, "Question", ""};
  timedChoice.choices = {{"Yes", -1}};
  vn.loadScript(
      {{200, NodeType::Dialogue, "Name", "Hello {player}", "", "", {}, 201},
       {201, NodeType::Choice, "", "", "", "", "Question", {{"Yes", 202}}, -1},
       {202,
        TimelineScript{3,
                       {{0, 2, CueType::Subtitle, "A=B", ""},
                        {0, 2, CueType::Dialogue, "Text box", "Name"},
                        timedChoice}},
        -1}});
  vn.setVariable("player", "Alice");
  vn.startScript(200);
  require(box.getText() == "Ola Alice" && box.getSpeaker() == "Nome",
          "Translate before expanding game variables");
  require(vn.getScriptNode(200).text == "Hello {player}",
          "Translation never mutates original node");
  vn.jumpToNode(201);
  require(choice.getQuestion() == "Pergunta" &&
              choice.getOptions()[0].label == "Sim",
          "Standard choices use node translations");
  vn.jumpToNode(202);
  require(timeline.activeSubtitleText() == "Legenda\ntraduzida",
          "JSON subtitles preserve equals and decode newlines");
  require(box.getText() == "Caixa traduzida" && box.isVisible(),
          "Timed textbox is translated separately from subtitles");
  require(choice.getOptions()[0].label == "Sim temporal",
          "Timed choice translation");
  require(engine.GetTranslation().text(999, "Global") == "Geral" &&
              engine.GetTranslation().text(999, "Missing") == "Missing",
          "Fallback dictionary and missing keys");
  timeline.stop();
  TemporaryScript malformed;
  Translation translation;
  malformed.write(
      R"({"200":{"A=B":"line\n\"quoted\"\\path"," padded ":" kept ","empty":"","Snow":"\u96ea","\\u0000":"literal escape"},"nodo":{"Global":"Shared"}})");
  translation.load(malformed.path.string());
  require(translation.text(200, "A=B") == "line\n\"quoted\"\\path" &&
              translation.text(200, " padded ") == " kept " &&
              translation.text(200, "empty").empty() &&
              translation.text(200, "Snow") == "\xe9\x9b\xaa" &&
              translation.text(200, "\\u0000") == "literal escape" &&
              translation.text(999, "Global") == "Shared",
          "JSON strings preserve punctuation, edge spaces and empty values");
  for (const char *invalid :
       {"[200]\nx=one\n", "[]", "null", R"({"200":[]})",
        R"({"name":{"x":"y"}})", R"({"01":{"x":"y"}})", R"({"-1":{"x":"y"}})",
        R"({"2147483648":{"x":"y"}})", R"({"200":{"":""}})",
        R"({"200":{"x":1}})", R"({"200":{"x":null}})", R"({"200":{"x":"y",}})",
        R"({"200":{"x":"y"}} trailing)", R"({"200":{"x":"\u0000"}})",
        R"({"200":{"x\u0000y":"z"}})", "{\"200\":{\"x\":\"\xff\"}}"}) {
    malformed.write(invalid);
    rejects([&] { translation.load(malformed.path.string()); },
            "Invalid JSON dictionaries are rejected");
    require(translation.text(200, " padded ") == " kept ",
            "Failed load preserves the previous dictionary");
  }
  malformed.write(R"({"200":{"x":"first","x":"last"}})");
  translation.load(malformed.path.string());
  require(translation.text(200, "x") == "last",
          "JSON duplicate keys consistently keep the last occurrence");
}

void testMasterModesAndSave(Kotonoha::Engine &engine) {
  using namespace Kotonoha;
  auto &vn = GetVNManagerRuntime(engine);
  auto &runtime = engine.GetRuntime();
  auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
  auto &choice = GetChoiceQuestionRuntime(engine);
  auto &timeline = engine.GetTimeline();
  ScriptNode dialogue{300, NodeType::Dialogue, "Name", "Before", "", "", {},
                      301};
  dialogue.duration = 1;
  ScriptNode question{301, NodeType::Choice,         "", "", "", "",
                      "Q", {{"A", 302}, {"B", 303}}, 302};
  question.duration = 1;
  ScriptNode after{302, NodeType::Dialogue, "Name", "After", "", "", {}, -1};
  after.duration = 1;
  ScriptNode other{303, NodeType::Dialogue, "Name", "Other", "", "", {}, -1};
  other.duration = 1;
  auto graph = std::vector<ScriptNode>{dialogue, question, after, other};

  vn.loadScript(graph, ScriptMode::Standard);
  vn.startScript(300);
  require(vn.getMasterMode() == ScriptMode::Standard && !timeline.isPlaying(),
          "Standard master runs on input only");
  vn.update();
  require(vn.getCurrentNodeId() == 300,
          "Standard dialogue does not auto-advance");
  TemporaryScript file;
  vn.save(file.path.string(), "inventory\n\"quoted\"");
  const auto saved = VNSave::read(file.path.string());
  vn.jumpToNode(303);
  vn.restoreSave(saved);
  require(vn.getCurrentNodeId() == 300 &&
              saved.gameData == "inventory\n\"quoted\"",
          "Save restores current standard node and arbitrary game data");
  vn.save(file.path.string(), "new payload");
  require(VNSave::read(file.path.string()).gameData == "new payload",
          "Atomic save replaces existing slot");
  auto invalid = saved;
  invalid.nodeId = 9999;
  rejects([&] { vn.restoreSave(invalid); }, "Unknown saved node rejected");
  require(vn.getCurrentNodeId() == 300,
          "Rejected save leaves current node intact");
  for (const char *bad :
       {"", "KOTONOHA_SAVE 2\n", "KOTONOHA_SAVE 1\n300 9 0\n0\n0 -1\n\"x\"\n",
        "KOTONOHA_SAVE 1\n300 0 -1\n0\n0 -1\n\"x\"\n"}) {
    file.write(bad);
    rejects([&] { VNSave::read(file.path.string()); }, "Corrupt save rejected");
  }

  vn.loadScript(graph, ScriptMode::Timed);
  vn.startScript(300);
  require(box.isDialogueComplete() && timeline.isPlaying(),
          "Timed text appears complete");
  vn.advance();
  require(vn.getCurrentNodeId() == 300, "Timed master ignores manual advance");
  timeline.update(1);
  vn.update();
  require(vn.getCurrentNodeId() == 301 && choice.isActive(),
          "Timed master advances dialogue at duration");
  timeline.update(10);
  require(timeline.seconds() == 1 && timeline.isWaitingForChoice() &&
              timeline.isPlaying(),
          "Unanswered question freezes at exact deadline");
  timeline.setPaused(false);
  timeline.update(1);
  require(timeline.seconds() == 1,
          "Overlay resume cannot unpause unanswered question");
  const auto waiting = vn.captureSave("paused question");
  vn.save(file.path.string(), waiting.gameData);
  vn.jumpToNode(302);
  vn.restoreSave(VNSave::read(file.path.string()));
  require(vn.getCurrentNodeId() == 301 && timeline.isWaitingForChoice() &&
              choice.isActive(),
          "Restore unanswered timed question at deadline");
  runtime.onEvent(key(SDLK_DOWN));
  runtime.onEvent(key(SDLK_RETURN));
  const auto selected = vn.captureSave();
  require(selected.pendingBranch == 303,
          "Save captures a pending choice branch");
  vn.restoreSave(selected);
  vn.update();
  require(vn.getCurrentNodeId() == 303,
          "Restored choice follows selected route");

  question.timeout = ChoiceTimeout::Continue;
  graph[1] = question;
  vn.loadScript(graph, ScriptMode::Timed);
  vn.startScript(301);
  timeline.update(5);
  const auto completedQuestion = vn.captureSave();
  vn.restoreSave(completedQuestion);
  vn.update();
  require(vn.getCurrentNodeId() == 302 && !choice.isActive(),
          "Game can continue after unanswered question");
  question.timeout = ChoiceTimeout::Select;
  question.timeoutTargetNodeId = 303;
  graph[1] = question;
  vn.loadScript(graph, ScriptMode::Timed);
  vn.startScript(301);
  timeline.update(5);
  vn.update();
  require(vn.getCurrentNodeId() == 303,
          "Game can choose default timeout route");
  question.duration = 0.01;
  graph[1] = question;
  vn.loadScript(graph, ScriptMode::Timed);
  vn.startScript(301);
  SDL_Delay(20);
  auto late = key(SDLK_RETURN);
  late.key.windowID = SDL_GetWindowID(&engine.GetWindow());
  engine.Event(&late);
  vn.update();
  require(vn.getCurrentNodeId() == 303,
          "Input dispatch applies timeout before accepting a late answer");

  TimelineCue first{0, 1, CueType::Choice, "First", ""};
  first.choices = {{"A", 302}};
  TimelineCue second{1, 1, CueType::Choice, "Second", ""};
  second.choices = {{"B", 303}};
  timeline.play({3, {first, second}});
  timeline.update(2.5);
  require(timeline.seconds() == 1 && choice.getQuestion() == "First",
          "Adjacent timed questions cannot replace unanswered question");
  timeline.stop();
  first.timeout = ChoiceTimeout::Continue;
  timeline.play({3, {first, second}});
  timeline.update(2.5);
  require(timeline.seconds() == 2 && choice.getQuestion() == "Second",
          "A large frame evaluates every question deadline");
  timeline.stop();
  auto noDuration = dialogue;
  noDuration.duration = 0;
  rejects([&] { vn.loadScript({noDuration}, ScriptMode::Timed); },
          "Timed master requires valid durations");
}

void testDebugTools(Kotonoha::Engine &engine) {
  using namespace Kotonoha;
  auto &manager = GetVNManagerRuntime(engine);
  auto &debug = engine.GetDebug();
  auto &timeline = engine.GetTimeline();
  auto &runtime = engine.GetRuntime();
  const std::string unusual = " #key=with\\slash\nand a trailing space ";
  ScriptNode dialogue{
      200, NodeType::Dialogue, "Name", "Hello {player}", "", "", {}, 201};
  dialogue.name = "Debug / dialogue";
  ScriptNode choices{201,        NodeType::Choice,
                     "",         "",
                     "",         "",
                     "Question", {{"Yes", 203}, {unusual, 200}},
                     -1};
  ScriptNode timed{
      203,
      TimelineScript{4,
                     {{0, 4, CueType::Dialogue, "Text box", "Name"},
                      {0, 4, CueType::Subtitle, "Separate subtitle", ""}}},
      200};
  ScriptNode untranslated{
      204, NodeType::Dialogue, "#Speaker", unusual, "", "", {}, 200};
  ScriptNode media{
      202,
      TimelineScript{2,
                     {{0, 2, CueType::Audio, "do-not-export-this-path.ogg", ""},
                      {0, 2, CueType::Subtitle, "Subtitle source", ""}}},
      200};
  manager.loadScript({timed, untranslated, choices, dialogue, media});
  manager.setVariable("player", "Alice");
  require(manager.getNodeIds() == std::vector<int>({200, 201, 202, 203, 204}),
          "Debug node list is complete and sorted");
  manager.startScript(200);
  TemporaryScript dictionary;
  manager.dumpTranslation(dictionary.path.string());
  Translation exported;
  exported.load(dictionary.path.string());
  require(
      exported.text(200, "Hello {player}") == "Ola {player}",
      "Dump preserves loaded translations and original variable placeholders");
  require(exported.text(204, unusual) == unusual &&
              exported.text(204, "#Speaker") == "#Speaker",
          "Exported JSON roundtrips punctuation, escapes and boundary spaces");
  std::ifstream contents(dictionary.path);
  std::string dump((std::istreambuf_iterator<char>(contents)), {});
  require(dump.find("do-not-export-this-path") == std::string::npos &&
              dump.find("Separate subtitle") != std::string::npos &&
              dump.find("Subtitle source") != std::string::npos,
          "Dump includes subtitles but excludes media paths");
  manager.dumpTranslation(dictionary.path.string());
  rejects(
      [&] { manager.dumpTranslation("/missing/kotonoha/translation.json"); },
      "Export failure is explicit");

  require(debug.nodeClockText().find("Node: 200") != std::string::npos &&
              debug.nodeClockText().find("Clock:") == std::string::npos,
          "Standard HUD shows node without a timeline clock");
  auto event = key(SDLK_F10);
  event.key.windowID = SDL_GetWindowID(&engine.GetWindow());
  engine.Event(&event);
  require(debug.isVisible() && engine.isStoryPaused(),
          "F10 opens engine-owned inspector");
  engine.Iterate();
  const auto before = manager.getCurrentNodeId();
  auto space = key(SDLK_SPACE);
  space.key.windowID = event.key.windowID;
  engine.Event(&space);
  require(manager.getCurrentNodeId() == before,
          "Debug consumes game keyboard input");
  rejects([&] { manager.debugJumpToNode(98765); },
          "Debug rejects unknown destinations");
  require(manager.getCurrentNodeId() == before,
          "Invalid jump does not affect current state");
  manager.debugJumpToNode(203);
  engine.Iterate();
  require(timeline.isPaused() &&
              debug.nodeClockText().find("Clock:") != std::string::npos,
          "Debug navigation exposes and pauses the selected timeline clock");
  const auto position = timeline.seconds();
  SDL_Delay(20);
  engine.Iterate();
  require(timeline.seconds() == position,
          "Inspector freezes the timeline while open");
  engine.Event(&event);
  require(!debug.isVisible() && !engine.isStoryPaused(),
          "F10 closes inspector");
  auto &dialog = GetTextInputDialogRuntime(engine, "Modal", nullptr, nullptr);
  runtime.AddScene(&dialog);
  runtime.update();
  require(SDL_TextInputActive(&engine.GetWindow()), "Modal starts input");
  manager.debugJumpToNode(200);
  require(!SDL_TextInputActive(&engine.GetWindow()) &&
              std::find(runtime.GetActiveScenes().begin(),
                        runtime.GetActiveScenes().end(),
                        &dialog) == runtime.GetActiveScenes().end(),
          "Debug navigation cleans up the old node modal");
  debug.getSettings().showFPS = false;
  debug.getSettings().showNodeClock = false;
  engine.Iterate();
  debug.getSettings() = {};
  timeline.stop();
}

void testDemoStory(Kotonoha::Engine &engine) {
  using namespace Kotonoha;
  using namespace KotonohaExample;
  DemoState state;
  int nameRequests = 0;
  auto &manager = GetVNManagerRuntime(engine);
  auto &timeline = engine.GetTimeline();
  auto &runtime = engine.GetRuntime();
  auto &choice = GetChoiceQuestionRuntime(engine);
  auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
  manager.loadScript(buildDemoStory(state, [&] {
    ++nameRequests;
    manager.setInputBlocked(true);
  }));
  manager.startScript(StoryNode::Opening);
  timeline.update(6);
  manager.update();
  require(manager.getCurrentNodeId() == StoryNode::Camp &&
              manager.getMode() == ScriptMode::Standard,
          "Demo switches from timed opening to traditional camp");
  box.completeDialogue();
  manager.advance();
  box.completeDialogue();
  manager.advance();
  require(nameRequests == 1,
          "Demo name action is dispatched through the engine");
  state.playerName = "Alice";
  manager.setVariable("player", state.playerName);
  manager.jumpToNode(StoryNode::Greeting);
  box.completeDialogue();
  manager.advance();
  runtime.onEvent(key(SDLK_RETURN));
  require(state.hasTorch && manager.getCurrentNodeId() == 7,
          "Traditional equipment choice executes inventory action");
  box.completeDialogue();
  manager.advance();
  require(manager.getCurrentNodeId() == StoryNode::Storm &&
              manager.getMode() == ScriptMode::Timed,
          "Demo enters timed storm");
  timeline.update(20);
  require(timeline.isWaitingForChoice() && timeline.seconds() == 7 &&
              choice.isActive(),
          "Storm waits at unanswered question deadline");
  runtime.onEvent(key(SDLK_RETURN));
  manager.update();
  require(manager.getCurrentNodeId() == 31,
          "Timed choice returns to traditional response");
  box.completeDialogue();
  manager.advance();
  runtime.onEvent(key(SDLK_RETURN));
  require(manager.getCurrentNodeId() == StoryNode::Cave,
          "Route choice opens timed cave scene");
  timeline.update(5);
  manager.update();
  require(manager.getCurrentNodeId() == 14, "Route checks preserved inventory");
  box.completeDialogue();
  manager.advance();
  require(manager.getCurrentNodeId() == StoryNode::Escape,
          "Demo reaches timed bridge choice");
  runtime.onEvent(key(SDLK_RETURN));
  manager.update();
  require(state.trustLevel == 1 && manager.getCurrentNodeId() == 43,
          "Answering bridge question affects game state");
  box.completeDialogue();
  manager.advance();
  require(manager.getCurrentNodeId() == StoryNode::GoodEnding,
          "Helping companion reaches traditional good ending");
  manager.debugJumpToNode(StoryNode::Escape);
  timeline.update(10);
  manager.update();
  require(state.trustLevel == -1 && manager.getCurrentNodeId() == 44,
          "Bridge timeout chooses game-defined alternative route");
  box.completeDialogue();
  manager.advance();
  require(manager.getCurrentNodeId() == StoryNode::BadEnding,
          "Timeout route reaches a different traditional ending");
}

class SpaceProbe : public Kotonoha::Scene {
  Kotonoha::RenderSpace space;

public:
  int renders = 0, events = 0;
  SDL_FPoint view{}, mouse{};
  SDL_FRect presentation{};
  float density = 0;
  explicit SpaceProbe(Kotonoha::Engine &engine, Kotonoha::RenderSpace space)
      : Scene(engine), space(space) {}
  Kotonoha::RenderSpace renderSpace() const override { return space; }
  bool handleEvent(Kotonoha::EventRef event) override {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      ++events;
      mouse = {event.button.x, event.button.y};
    }
    return false;
  }
  void render(Kotonoha::RendererRef renderer, float) override {
    ++renders;
    view = Kotonoha::UI::viewSize(renderer);
    density = engine.GetUI().pixelDensity();
    require(SDL_GetRenderLogicalPresentationRect(&renderer, &presentation),
            "Read presentation bounds");
    if (space == Kotonoha::RenderSpace::Novel) {
      const SDL_Rect clip{0, 0, 1, 1};
      require(SDL_SetRenderClipRect(&renderer, &clip),
              "Set novel-only clipping");
    } else {
      require(!SDL_RenderClipEnabled(&renderer),
              "Novel clipping must not affect screen HUD");
    }
  }
};

void testHiDPIAndSpaces(Kotonoha::Engine &engine) {
  using namespace Kotonoha;
  auto &runtime = engine.GetRuntime();
  runtime.ClearScenes();
  engine.GetTimeline().stop();
  SpaceProbe novel(engine, RenderSpace::Novel),
      screen(engine, RenderSpace::Screen);
  runtime.AddScene(&novel);
  runtime.AddScene(&screen);
  require(engine.GetDebug().getSettings().showFPS,
          "Engine initializes FPS without game registration");
  require(SDL_SetWindowSize(&engine.GetWindow(), 1000, 1000),
          "Resize test window");
  SDL_PumpEvents();
  engine.Iterate();
  require(novel.renders == 1 && screen.renders == 1,
          "Each scene renders once in its space");
  require(novel.view.x == 1280 && novel.view.y == 720 &&
              screen.view.x == 1000 && screen.view.y == 1000,
          "Independent coordinate spaces");
  require(novel.presentation.y > 0 && screen.presentation.y == 0,
          "Window HUD extends above the novel letterbox");
  require(std::abs(novel.density - novel.presentation.w / 1280) < 0.001f &&
              std::abs(screen.density - screen.presentation.w / 1000) < 0.001f,
          "Each space uses physical pixels per logical unit");
  auto event = click(10, 10);
  event.button.windowID = SDL_GetWindowID(&engine.GetWindow());
  engine.Event(&event);
  require(screen.events == 1 && novel.events == 0 &&
              std::abs(screen.mouse.x - 10) < 0.1f &&
              std::abs(screen.mouse.y - 10) < 0.1f,
          "Letterbox clicks go only to screen UI");
  event = click(500, 500);
  event.button.windowID = SDL_GetWindowID(&engine.GetWindow());
  engine.Event(&event);
  require(novel.events == 1 && std::abs(novel.mouse.x - 640) < 0.1f &&
              std::abs(novel.mouse.y - 360) < 0.1f,
          "Novel input converts from window coordinates");
  auto &ui = engine.GetUI();
  ui.setPixelDensity(2);
  require(ui.rasterFontSize() == 48,
          "HiDPI rasterizes text at 2x instead of stretching 24px");
  const float height2 = ui.textHeight(engine.GetRenderer(), "HiDPI", 300);
  ui.setPixelDensity(1);
  const float height1 = ui.textHeight(engine.GetRenderer(), "HiDPI", 300);
  require(std::abs(height2 - height1) <= 2,
          "HiDPI keeps logical text size stable");
  rejects([&] { ui.setPixelDensity(0); }, "Invalid density rejected");
  require(SDL_SetWindowSize(&engine.GetWindow(), 1600, 700),
          "Resize wide test window");
  SDL_PumpEvents();
  engine.Iterate();
  require(novel.presentation.x > 0 && screen.presentation.x == 0,
          "Window HUD extends beside a pillarboxed novel");
  runtime.RemoveScene(&novel);
  runtime.RemoveScene(&screen);
  auto &dialog = GetTextInputDialogRuntime(engine, "IME", nullptr, nullptr);
  runtime.AddScene(&dialog);
  engine.Iterate();
  SDL_Rect area{};
  require(SDL_GetTextInputArea(&engine.GetWindow(), &area, nullptr),
          "Read IME input rectangle");
  float x = 0, y = 0;
  require(
      SDL_RenderCoordinatesToWindow(&engine.GetRenderer(), 400, 330, &x, &y),
      "Convert logical text input to window coordinates");
  require(std::abs(area.x - x) <= 1 && std::abs(area.y - y) <= 1,
          "IME follows letterbox and HiDPI transforms");
  require(SDL_StopTextInput(&engine.GetWindow()), "Stop test IME");
  runtime.ClearScenes();
}

void testRuntime(Kotonoha::Engine &engine) {
  using namespace Kotonoha;
  auto &runtime = engine.GetRuntime();
  auto &vn = GetVNManagerRuntime(engine);
  auto &box = GetVNTextBoxRuntime(engine, "", "", 30);
  auto &choices = GetChoiceQuestionRuntime(engine);
  auto &backlog = GetVNBacklogRuntime(engine);
  auto &menu = GetInGameMenuRuntime(engine);
  runtime.AddScene(&vn);
  runtime.AddScene(&box);
  runtime.AddScene(&choices);
  runtime.AddScene(&backlog);
  runtime.AddScene(&menu);
  require(&runtime.GetScenes(GameState::Splash) !=
              &runtime.GetScenes(GameState::Menu),
          "State scene lists stay separate");

  const std::string utf8 = "a\xc3\xa7\xe6\x97\xa5\xf0\x9f\x8c\x99";
  require(utf8Prefix(utf8, 2) == 3, "UTF-8 character prefix");
  std::string edited = utf8;
  eraseLastCharacter(edited);
  require(edited == utf8.substr(0, 6), "UTF-8 backspace");
  engine.GetUI().text(engine.GetRenderer(), utf8, 0, 0, 100);

  vn.loadScript({{0, NodeType::Dialogue, "Name", utf8, "", "", {}, 1},
                 {1,
                  NodeType::Choice,
                  "",
                  "",
                  "",
                  "",
                  "Choose",
                  {{"First", 2}, {"Second", 3}},
                  -1},
                 {2, NodeType::Dialogue, "A", "A", "", "", {}, -1},
                 {3, NodeType::Dialogue, "B", "B", "", "", {}, -1}});
  vn.startScript();
  runtime.onEvent(key(SDLK_B));
  require(backlog.isVisible(), "B opens backlog");
  runtime.onEvent(click(100, 100));
  require(vn.getCurrentNodeId() == 0 && !box.isDialogueComplete(),
          "Backlog absorbs clicks");
  runtime.onEvent(key(SDLK_ESCAPE));
  require(!backlog.isVisible() && !menu.IsOpen(), "ESC closes only backlog");
  runtime.onEvent(click(930, 520));
  require(menu.IsOpen(), "Textbox menu button works");
  require(!box.isDialogueComplete(), "Menu button does not advance dialogue");
  runtime.onEvent(key(SDLK_ESCAPE));
  runtime.onEvent(key(SDLK_SPACE));
  require(box.isDialogueComplete() && vn.getCurrentNodeId() == 0,
          "First advance completes text");
  runtime.onEvent(key(SDLK_SPACE));
  require(choices.isActive() && vn.getCurrentNodeId() == 1, "Choice shown");
  runtime.onEvent(key(SDLK_ESCAPE));
  require(menu.IsOpen(), "Pause menu is accessible during a choice");
  runtime.onEvent(key(SDLK_ESCAPE));
  runtime.onEvent(key(SDLK_DOWN));
  runtime.onEvent(key(SDLK_RETURN));
  require(vn.getCurrentNodeId() == 3 && !box.isDialogueComplete(),
          "Choice selects once without click-through");
  runtime.onEvent(key(SDLK_SPACE));
  runtime.onEvent(key(SDLK_SPACE));
  require(vn.isFinished(), "Final dialogue finishes only after confirmation");

  std::string name;
  auto &dialog = GetTextInputDialogRuntime(
      engine, "Name",
      [&](const std::string &value) {
        name = value;
        runtime.RemoveScene(
            &GetTextInputDialogRuntime(engine, "", nullptr, nullptr));
      },
      nullptr);
  runtime.AddScene(&dialog);
  runtime.update();
  SDL_Event text{};
  text.type = SDL_EVENT_TEXT_INPUT;
  text.text.text = utf8.c_str();
  runtime.onEvent(text);
  runtime.onEvent(key(SDLK_BACKSPACE));
  runtime.onEvent(key(SDLK_RETURN));
  require(name == edited, "SDL text input and UTF-8 deletion");
  require(!SDL_TextInputActive(&engine.GetWindow()),
          "Text input stops after submit");

  auto &timeline = engine.GetTimeline();
  TimelineScript timed{4,
                       {{1, 1, CueType::Dialogue, "Timed", "Narrator"},
                        {0, 1, CueType::Subtitle, "Subtitle", ""},
                        {3, 1, CueType::Subtitle, "End", ""}}};
  box.hide();
  timeline.play(timed);
  SDL_SetRenderDrawColor(&engine.GetRenderer(), 0, 0, 0, 255);
  SDL_RenderClear(&engine.GetRenderer());
  const auto blank = fingerprint(engine.GetRenderer());
  timeline.renderSubtitles(engine.GetRenderer());
  require(fingerprint(engine.GetRenderer()) != blank,
          "Subtitle renders at its start");
  timeline.update(0.999);
  require(!box.isVisible(), "Cue must not fire early");
  timeline.update(0.001);
  require(box.isVisible() && box.isDialogueComplete(),
          "Cue fires at exact start");
  SDL_SetRenderDrawColor(&engine.GetRenderer(), 0, 0, 0, 255);
  SDL_RenderClear(&engine.GetRenderer());
  timeline.renderSubtitles(engine.GetRenderer());
  require(fingerprint(engine.GetRenderer()) == blank,
          "Subtitle disappears at exact end");
  timeline.setPaused(true);
  timeline.update(10);
  require(std::abs(timeline.seconds() - 1) < 1e-9, "Pause freezes position");
  timeline.setPaused(false);
  timeline.update(1);
  require(!box.isVisible(), "Cue expires at exact end");
  timeline.update(2);
  require(!timeline.isPlaying(), "Timeline finishes at exact duration");
  rejects([&] { timeline.update(-1); }, "Negative delta rejected");
  rejects([&] { timeline.update(std::nan("")); }, "NaN delta rejected");

  vn.loadScript({ScriptNode{10, timed, 11},
                 {11, NodeType::Dialogue, "Name", "After", "", "", {}, -1}});
  vn.startScript(10);
  runtime.onEvent(key(SDLK_SPACE));
  require(vn.getCurrentNodeId() == 10,
          "Manual advance blocked during timeline");
  runtime.onEvent(key(SDLK_B));
  engine.Iterate();
  require(timeline.isPaused(), "Backlog pauses timeline");
  runtime.onEvent(key(SDLK_ESCAPE));
  engine.Iterate();
  require(!timeline.isPaused(), "Closing backlog resumes timeline");
  timeline.update(4);
  runtime.update();
  require(vn.getCurrentNodeId() == 11,
          "Timeline automatically follows next node");
  timeline.play(timed);
  timeline.update(2);
  timeline.play(timed);
  require(timeline.seconds() == 0, "Restart resets timeline position");
  timeline.stop();
  require(!timeline.isPlaying(), "Stop terminates timeline");
  timeline.play({0.3, {{0.1, 0.2, CueType::Dialogue, "Decimal", "Name"}}});
  timeline.update(0.1);
  timeline.update(0.2);
  require(!timeline.isPlaying(),
          "Decimal durations finish without an extra tick");
  timeline.play(timed);
  timeline.update(2.5);
  require(!box.isVisible(),
          "Expired dialogue cues are skipped after a long frame");
  timeline.stop();
  rejects([&] { vn.jumpToNode(999); }, "Unknown node fails explicitly");
  rejects(
      [&] {
        vn.loadScript(
            {ScriptNode{0, NodeType::Dialogue, "", "", "", "", {}, 9}});
      },
      "Invalid script links rejected");

  // Exercise wrapping, clipping, long option lists and overlays on a real SDL
  // renderer.
  std::vector<ChoiceOption> many;
  for (int i = 0; i < 30; ++i)
    many.push_back({std::string(200, 'a'), i});
  choices.setQuestion("Long list", many, nullptr);
  for (int i = 0; i < 30; ++i)
    choices.onEvent(key(SDLK_DOWN));
  choices.render(engine.GetRenderer(), 1);
  choices.hide();
  backlog.open();
  backlog.render(engine.GetRenderer(), 1);
  backlog.close();
  engine.Iterate();
}

void testMedia(Kotonoha::Engine &engine, const char *path) {
  using namespace Kotonoha;
  MediaPlayer video;
  rejects([&] { video.open("/missing/kotonoha.mp4", false); },
          "Missing media reported explicitly");
  video.open(path, false);
  video.update(engine.GetRenderer(), 0);
  require(video.hasVideoFrame(), "Video decodes first frame");
  SDL_SetRenderDrawColor(&engine.GetRenderer(), 0, 0, 0, 255);
  SDL_RenderClear(&engine.GetRenderer());
  video.render(engine.GetRenderer());
  const auto firstFrame = fingerprint(engine.GetRenderer());
  SurfacePtr image{SDL_RenderReadPixels(&engine.GetRenderer(), nullptr)};
  require(static_cast<bool>(image), "Read back decoded video");
  bool colored = false;
  for (int y = 0; y < image->h && !colored; y += 10)
    for (int x = 0; x < image->w && !colored; x += 10) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      require(SDL_ReadSurfacePixel(image.get(), x, y, &r, &g, &b, &a),
              "Read video pixel");
      colored = r > 20 || g > 20 || b > 20;
    }
  require(colored, "Decoded video must actually render nonblack pixels");
  video.setPaused(true);
  video.setPaused(false);
  for (int i = 1; i <= 30; ++i) {
    SDL_Delay(34);
    video.update(engine.GetRenderer(), i / 30.0);
  }
  video.render(engine.GetRenderer());
  require(fingerprint(engine.GetRenderer()) != firstFrame,
          "Video must advance beyond its first frame");
  video.open(path, false);
  video.update(engine.GetRenderer(), 0.5);
  require(video.hasVideoFrame(),
          "Restored media decodes at nonzero timeline offset");
  video.update(engine.GetRenderer(), 0.1);
  require(video.hasVideoFrame(), "Backward timeline changes seek the media");
  video.stop();
  require(!video.hasVideoFrame(), "Stop releases video texture");
  MediaPlayer audio;
  audio.open(path, true);
  for (int i = 0; i <= 30; ++i) {
    SDL_Delay(34);
    audio.update(engine.GetRenderer(), i / 30.0);
  }
  audio.stop();
  auto &timeline = engine.GetTimeline();
  timeline.play(
      {1,
       {{0, 1, CueType::Video, path, ""}, {0, 1, CueType::Audio, path, ""}}});
  timeline.update(0.5);
  timeline.setPaused(true);
  timeline.setPaused(false);
  timeline.update(0.5);
  require(!timeline.isPlaying(), "Timed media is stopped at cue end");
}

} // namespace

int main(int argc, char **argv) {
  try {
    testParser();
    TemporaryScript dictionary;
    dictionary.write("\xef\xbb\xbf"
                     R"({
      "200": {"Hello {player}": "Ola {player}", "Name": "Nome"},
      "201": {"Question": "Pergunta", "Yes": "Sim"},
      "202": {"A=B": "Legenda\ntraduzida", "Text box": "Caixa traduzida",
              "Yes": "Sim temporal"},
      "nodo": {"Global": "Geral"}
    })");
    TestRuntime runtime;
    Kotonoha::Engine engine(runtime, {dictionary.path.string()});
    testRuntime(engine);
    testTranslations(engine);
    testMasterModesAndSave(engine);
    testDebugTools(engine);
    testDemoStory(engine);
    if (argc > 1)
      testMedia(engine, argv[1]);
    testHiDPIAndSpaces(engine);
    std::cout << "All runtime tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
