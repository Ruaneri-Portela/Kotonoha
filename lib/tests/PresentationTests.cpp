#include <Kotonoha/componentes/FadeScene.hpp>
#include <Kotonoha/componentes/ForcedSubtitleRenderer.hpp>
#include <Kotonoha/componentes/InGameMenu.hpp>
#include <Kotonoha/componentes/SettingsMenu.hpp>
#include <Kotonoha/componentes/SubtitleScene.hpp>
#include <Kotonoha/componentes/VNManager.hpp>
#include <Kotonoha/componentes/VNTextBox.hpp>
#include <Kotonoha/componentes/VNBacklog.hpp>
#include <Kotonoha/core/CommandLine.hpp>
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/UI.hpp>
#include <Kotonoha/debug/DebugOverlay.hpp>
#include <Kotonoha/script/TimelinePlayer.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace Kotonoha;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message) {
  bool failed = false;
  try {
    action();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, message);
}
void parse(CommandLine &parser, std::initializer_list<const char *> values) {
  std::vector<std::string> strings;
  for (const char *value : values)
    strings.emplace_back(value);
  std::vector<char *> argv;
  for (auto &value : strings)
    argv.push_back(value.data());
  parser.parse(static_cast<int>(argv.size()), argv.data());
}
void arguments() {
  EngineOptions options;
  options.persistSettings = true;
  CommandLine parser("TestGame", options);
  std::string game;

  parser.option("--game", "DIRECTORY", "Game-specific data", game);
  parser.alias("--game", "--assets");

  parse(parser,
        {"game", "--assets=data", "--subtitle-style", "style.ass", "--frames",
         "12", "--no-config", "--mcp-root", "/tmp", "--launcher", "-h"});
  require(game == "data" && options.launcher && parser.frames == 12 &&
              options.subtitleStylePath == "style.ass" &&
              !options.persistSettings && options.mcpFileRoot == "/tmp" &&
              parser.wantsHelp(),
          "Shared and game-specific arguments and aliases");
  const auto help = parser.help();
  require(help.find("--game DIRECTORY") != std::string::npos &&
              help.find("--subtitle-style FILE.ass") != std::string::npos,
          "Help includes engine and game options");
  parse(parser, {"game", "--ass-style=alias.ass"});
  require(options.subtitleStylePath == "alias.ass",
          "--ass-style aliases --subtitle-style");
  for (const char *count : {"0", "-1", "3oops", "99999999999999999999999999"})
    rejects([&] { parse(parser, {"game", "--frames", count}); },
            "Strict frame count");
  rejects([&] { parse(parser, {"game", "--unknown"}); }, "Unknown option");
  rejects([&] { parse(parser, {"game", "--game"}); }, "Missing game value");
  rejects([&] { parse(parser, {"game", "--game", "--help"}); },
          "Flag is not a value");
  rejects([&] { parse(parser, {"game", "--mcp=true"}); },
          "Flag does not accept a value");
  rejects([&] { parser.flag("--mcp", "duplicate", options.launcher); },
          "Duplicate registration");
}

std::string styleFile() {
  return "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n"
         "Title: Original script metadata\n[V4+ Styles]\n"
         "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
         "OutlineColour, "
         "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
         "Spacing, Angle, "
         "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, "
         "Encoding\n"
         "Style: Default,Noto Sans "
         "JP,46,&H000000FF,&H000000FF,&H00000000,&H00000000,"
         "0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
         "Style: Person,Noto Sans "
         "JP,46,&H0000FF00,&H000000FF,&H00000000,&H00000000,"
         "0,0,0,0,100,100,0,0,1,1,0,2,20,20,40,1\n"
         "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
         "MarginV, Effect, Text\n"
         "Dialogue: 0,0:00:00.00,0:01:00.00,Default,,0,0,0,,DISCARDED ORIGINAL "
         "TEXT\n"
         "[Fonts]\nfontname: NotImported.ttf\n";
}

struct TestRuntime : Runtime {
  std::string node = "custom-scene";
  std::optional<double> seconds = 12.375;
  bool timedClock = true;
  bool opaqueGameplay = false;
  int unpausedUpdates = 0;
  std::function<void(RendererRef)> inspectScreen;
  std::function<void(RendererRef)> inspectBeforeOverlay;
  TestRuntime()
      : Runtime("Presentation tests", "1", "org.kotonoha.presentation.tests." +
                    std::to_string(SDL_GetPerformanceCounter())) {}
  std::string debugNode() const override { return node; }
  bool usesTimedNodeClock() const override { return timedClock; }
  std::optional<double> debugNodeSeconds() const override { return seconds; }
  void update() override {
    Runtime::update();
    if (!isStoryPaused())
      ++unpausedUpdates;
  }
  SDL_FPoint sceneSize(const Scene &scene) const override {
    if (dynamic_cast<const ForcedSubtitleRenderer *>(&scene))
      return {640, 360};
    return Runtime::sceneSize(scene);
  }
  void renderGameplay(RendererRef renderer, float scale) override {
    Runtime::renderGameplay(renderer, scale);
    if (opaqueGameplay) {
      SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
      SDL_RenderFillRect(&renderer, nullptr);
    }
  }
  void renderScreen(RendererRef renderer, float scale) override {
    if (inspectScreen)
      inspectScreen(renderer);
    Runtime::renderScreen(renderer, scale);
  }
  void renderOverlay(RendererRef renderer, float scale) override {
    if (inspectBeforeOverlay)
      inspectBeforeOverlay(renderer);
    Runtime::renderOverlay(renderer, scale);
  }
};

int colored(RendererRef renderer, int color) {
  SurfacePtr image(SDL_RenderReadPixels(&renderer, nullptr));
  require(bool(image), "Read subtitle pixels");
  int result = 0;
  for (int y = 0; y < image->h; ++y)
    for (int x = 0; x < image->w; ++x) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      require(SDL_ReadSurfacePixel(image.get(), x, y, &r, &g, &b, &a),
              "Read pixel");
      if (color == 0 ? (r > 140 && g < 80) : (g > 140 && r < 80))
        ++result;
    }
  return result;
}
void subtitles(Engine &engine) {
  const auto style = AssStyle::parse(styleFile());
  require(style.header.find("Original script metadata") != std::string::npos &&
              style.names.size() == 2 &&
              style.header.find("DISCARDED") == std::string::npos &&
              style.header.find("[Events]") == std::string::npos &&
              style.header.find("[Fonts]") == std::string::npos,
          "Import script info and styles, discard all original texts and "
          "attachments");
  rejects([] { AssStyle::parse("[Script Info]\n[V4+ Styles]\n"); },
          "Missing style rejected");
  rejects([] { AssStyle::load("/nonexistent/style.ass"); },
          "Missing style file rejected");
  auto &renderer = engine.GetRenderer();
  SubtitleScene scene(engine);
  scene.setStyle(style);
  scene.setCues({{1, 2, "NEW CUE", "Person"}});
  const auto draw = [&](double position) {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    scene.setPosition(position);
    scene.render(renderer, 1);
  };
  draw(0.5);
  require(colored(renderer, 0) == 0 && colored(renderer, 1) == 0,
          "Imported dialogue was discarded; cue not started");
  draw(1.5);
  require(colored(renderer, 1) > 100 && colored(renderer, 0) == 0,
          "libass renders new text with the matching speaker style");
  draw(2);
  require(colored(renderer, 1) == 0, "End time is exclusive");
  draw(1.2);
  require(colored(renderer, 1) > 100, "Seeking backwards restores subtitle");
  auto beforeEdit = colored(renderer, 1);
  engine.GetRuntime().AddScene(&scene);
  engine.setTranslation("0", "NEW CUE", "X");
  scene.render(renderer, 1);
  draw(1.2);
  require(
      scene.getPosition() == 1.2 && colored(renderer, 1) < beforeEdit,
      "Live subtitle editing keeps the timeline and updates rendered glyphs");
  engine.GetRuntime().RemoveScene(&scene);

  SDL_SetRenderScale(&renderer, 0.5f, 0.5f);
  draw(1.5);
  require(colored(renderer, 1) > 20, "Subtitle resizes with renderer scale");
  SDL_SetRenderScale(&renderer, 1, 1);
  class DialogueSubtitles : public SubtitleScene {
  protected:
    bool replacesPreviousCue() const override { return true; }

  public:
    using SubtitleScene::SubtitleScene;
  } dialogue(engine);
  dialogue.setStyle(style);
  auto drawDialogue = [&](double seconds) {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    dialogue.setPosition(seconds);
    dialogue.render(renderer, 1);
    return colored(renderer, 1);
  };
  dialogue.setCues({{1, 5, "X", "Person"}});
  const auto singleLine = drawDialogue(1.5);
  dialogue.setCues(
      {{0, 5, "PREVIOUS LONG DIALOGUE", "Person"}, {1, 5, "X", "Person"}});
  require(drawDialogue(1.5) == singleLine,
          "Replacing dialogue must not stack overlapping libass events");
  require(drawDialogue(0.5) > singleLine,
          "Seeking backward restores the previous exclusive line");
  dialogue.setCues(
      {{1, 5, "REPLACED AT SAME TIME", "Person"}, {1, 5, "X", "Person"}});
  require(drawDialogue(1.5) == singleLine,
          "Equal-time dialogue keeps only the last line");
  rejects([&] { scene.setPosition(-1); }, "Reject negative subtitle clock");
  rejects([&] { scene.setCues({{2, 1, "bad", ""}}); }, "Reject reversed cue");
  scene.clear();
  draw(1.5);
  require(colored(renderer, 1) == 0, "Clearing removes the track");
}

void pauseSources(Engine &engine, TestRuntime &runtime) {
  auto &debug = engine.GetDebug();
  auto &menu = GetInGameMenuRuntime(engine);
  auto &timeline = engine.GetTimeline();
  const auto previousNode = runtime.node;
  runtime.node = "pause-test";
  runtime.timedClock = false;
  runtime.SetState(GameState::Gameplay);
  runtime.AddScene(&menu);
  engine.resetNodeClock();
  TimelineScript script;
  script.duration = 10;
  timeline.play(script);
  const auto checkPaused = [&] {
    require(engine.isStoryPaused() && runtime.isStoryPaused(),
            "Engine and native runtime share the same pause state");
    const auto timelineBefore = timeline.seconds();
    const auto clockBefore = engine.activeNodeSeconds();
    const auto updatesBefore = runtime.unpausedUpdates;
    SDL_Delay(20);
    engine.synchronizeTimeline();
    runtime.update();
    require(timeline.isPaused() && timeline.seconds() == timelineBefore &&
                engine.activeNodeSeconds() == clockBefore &&
                runtime.unpausedUpdates == updatesBefore,
            "All pause sources freeze the timeline, node clock and native updates");
  };
  const auto checkRunning = [&] {
    require(!engine.isStoryPaused() && !runtime.isStoryPaused(),
            "Engine and native runtime resume together");
    const auto timelineBefore = timeline.seconds();
    const auto clockBefore = engine.activeNodeSeconds();
    const auto updatesBefore = runtime.unpausedUpdates;
    SDL_Delay(20);
    engine.synchronizeTimeline();
    runtime.update();
    require(!timeline.isPaused() && timeline.seconds() > timelineBefore &&
                engine.activeNodeSeconds() > clockBefore &&
                runtime.unpausedUpdates > updatesBefore,
            "Unpaused timeline, node clock and native updates advance together");
  };
  engine.setAgentPaused(false);
  checkRunning();
  debug.setVisible(true);
  require(!runtime.hasPausingScene(),
          "Debug pause reaches native runtimes without a modal scene");
  checkPaused();
  menu.open();
  debug.setVisible(false);
  checkPaused();
  debug.setVisible(true);
  menu.close();
  checkPaused();
  debug.getSettings().ignorePause = true;
  checkRunning();
  menu.open();
  checkPaused();
  debug.setVisible(false);
  checkPaused();
  menu.close();
  checkRunning();
  engine.openSettings();
  checkPaused();
  engine.GetSettingsMenu().close();
  engine.setAgentPaused(true);
  checkPaused();
  debug.getSettings().ignorePause = false;
  debug.setVisible(true);
  rejects([&] { engine.advanceTimeline(0.1); },
          "MCP stepping cannot bypass debug pause");
  debug.setVisible(false);
  menu.open();
  rejects([&] { engine.advanceTimeline(0.1); },
          "MCP stepping cannot bypass an in-game menu");
  menu.close();
  engine.advanceTimeline(0.1);
  checkPaused();
  engine.setAgentPaused(false);
  checkRunning();
  debug.getSettings().ignorePause = false;
  runtime.RemoveScene(&menu);
  timeline.stop();
  runtime.node = previousNode;
  runtime.timedClock = true;
  engine.resetNodeClock();
}

void forcedSubtitles(Engine &engine, TestRuntime &runtime) {
  struct SubtitleDirectory {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("kotonoha-forced-" + std::to_string(SDL_GetPerformanceCounter()));
    SubtitleDirectory() {
      std::filesystem::create_directories(path);
      std::filesystem::current_path(path);
    }
    ~SubtitleDirectory() {
      std::error_code error;
      std::filesystem::current_path(previous, error);
      std::filesystem::remove_all(path, error);
    }
  } directory;
  auto &forced = engine.GetForcedSubtitles();
  const auto originalDirectory = forced.sourceDirectory();
  require(originalDirectory.is_absolute(),
          "Forced subtitle source is already resolved before loading any node");
  require(!std::filesystem::equivalent(originalDirectory.parent_path(),
                                       directory.path),
          "Changing the working directory does not change the startup source");
  auto &timeline = engine.GetTimeline();
  auto &manager = GetVNManagerRuntime(engine);
  runtime.SetState(GameState::Gameplay);
  runtime.node = "80005";
  runtime.opaqueGameplay = true;
  engine.setAgentPaused(true);
  engine.resetNodeClock();

  const auto header = styleFile().substr(0, styleFile().find("[Events]"));
  const auto script = header +
      "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
      "MarginV, Effect, Text\n"
      "Dialogue: 0,0:00:01.00,0:00:02.00,Person,,0,0,0,,FORCED EARLY\n"
      "Dialogue: 0,0:00:12.00,0:00:13.00,Person,,0,0,0,,FORCED NATIVE\n";
  std::filesystem::create_directories(originalDirectory / "nested");
  const auto originalPath = originalDirectory / "nested/80005.ass";
  const auto otherOriginal = originalDirectory / "80006.ass";
  writeTextFileAtomically(originalPath.string(), script);
  writeTextFileAtomically(otherOriginal.string(), script);
  forced.reload();
  forced.updateForActiveNode();
  require(forced.activePath() == originalPath,
          "Before dumping, automatic recursive detection loads the original ASS");

  const auto drawForced = [&] {
    auto &renderer = engine.GetRenderer();
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    forced.updateForActiveNode();
    forced.render(renderer, 1);
    return colored(renderer, 1);
  };
  require(drawForced() > 100, "Automatically detected original renders");
  writeTextFileAtomically(originalPath.string(), header);
  forced.reload();
  require(drawForced() == 0 && forced.activePath() == originalPath,
          "Reload without a dump reads edits from the original");
  writeTextFileAtomically(originalPath.string(), script);
  forced.reload();
  require(drawForced() > 100, "Original reload restores edited dialogue");

  const auto unrelatedDump = directory.path / "forced_subtitles/80006.ass";
  ForcedSubtitleRenderer missingSource(engine);
  require(missingSource.sourceDirectory().empty(),
          "Startup without a subtitle directory records no automatic source");
  std::filesystem::create_directories(unrelatedDump.parent_path());
  writeTextFileAtomically(unrelatedDump.string(), header);
  missingSource.reload();
  missingSource.loadForNode("80006");
  require(missingSource.sourceDirectory().empty() &&
              missingSource.activePath().empty(),
          "Reload does not redetect a directory created after startup");
  runtime.node = "80006";
  forced.reload();
  require(drawForced() > 100 && forced.activePath() == otherOriginal,
          "An existing working-directory ASS does not replace an undumped original");
  runtime.node = "80005";
  forced.updateForActiveNode();
  const auto path = forced.dumpForActiveNode();
  require(path.filename() == "80005.ass",
          "Timed native dump uses only the node ID");
  require(std::filesystem::equivalent(
              path.parent_path(), directory.path / "forced_subtitles"),
          "Forced subtitle dumps go to the current working directory");
  {
    std::ifstream input(path);
    const std::string content((std::istreambuf_iterator<char>(input)), {});
    require(content.find("PlayResX: 640\nPlayResY: 360\n") != std::string::npos,
            "ASS dump uses the native logical scene dimensions");
  }
  rejects([&] { forced.dumpForActiveNode(); },
          "Dump never overwrites existing subtitles");
  require(drawForced() == 0 && forced.activePath() == path,
          "After dumping, reload selects the dump instead of the original");
  writeTextFileAtomically(path.string(), script);
  forced.reload();
  require(drawForced() > 100 && forced.activePath() == path,
          "Reload follows edits to the dump");
  const auto newWorkingDirectory = directory.path / "changed-working-directory";
  std::filesystem::create_directory(newWorkingDirectory);
  std::filesystem::current_path(newWorkingDirectory);
  forced.reload();
  require(drawForced() > 100 && forced.activePath() == path,
          "Dump reload keeps its absolute path after working directory changes");
  std::filesystem::current_path(directory.path);
  std::filesystem::remove(path);
  forced.reload();
  require(drawForced() == 0 && forced.activePath().empty(),
          "A missing selected dump clears the active ASS instead of silently "
          "falling back to the original");
  writeTextFileAtomically(path.string(), script);
  forced.reload();
  require(drawForced() > 100 && forced.activePath() == path,
          "Restoring the dump allows reload to recover its active ASS path");
  runtime.node = "80006";
  forced.reload();
  require(drawForced() > 100 && forced.activePath() == otherOriginal,
          "Dump selection is per node and leaves other originals unchanged");
  runtime.node = "80005";
  forced.reload();
  int green = 0;
  runtime.inspectScreen = [&](RendererRef renderer) {
    green = colored(renderer, 1);
  };
  const auto frame = [&] {
    engine.Iterate();
    return green;
  };
  require(frame() > 100 && forced.getPosition() == 12.375,
          "Forced subtitles render above opaque native scenes without a timeline");

  runtime.seconds = 2;
  require(frame() == 0, "Native clock respects exclusive subtitle end");
  runtime.seconds = 1.5;
  require(frame() > 100 && forced.getPosition() == 1.5,
          "Native backward seek restores forced subtitles");
  runtime.node = "missing:67";
  require(frame() == 0 && forced.activePath().empty(),
          "Node changes clear stale forced subtitles and the active ASS path");
  runtime.node = "80005";
  require(frame() > 100, "Returning to a node reloads its subtitles");
  auto &menu = GetInGameMenuRuntime(engine);
  runtime.AddScene(&menu, GameState::Gameplay);
  int behindUI = 0;
  runtime.inspectBeforeOverlay = [&](RendererRef renderer) {
    behindUI = colored(renderer, 1);
  };
  menu.open();
  frame();
  require(behindUI > 100 && green < behindUI &&
              forced.getPosition() == 1.5,
          "Paused forced subtitles remain visible behind the in-game menu");
  menu.close();
  runtime.RemoveScene(&menu);
  require(frame() > 100, "Closing a native menu restores forced subtitles");
  auto &backlog = GetVNBacklogRuntime(engine);
  runtime.AddScene(&backlog, GameState::Gameplay);
  backlog.open();
  frame();
  require(behindUI > 100 && green < behindUI,
          "Backlog renders after, not underneath, forced subtitles");
  backlog.close();
  runtime.RemoveScene(&backlog);
  engine.openSettings();
  frame();
  require(behindUI > 100 && forced.getPosition() == 1.5,
          "Settings preserve paused subtitles in the underlying scene");
  engine.GetSettingsMenu().close();
  runtime.inspectBeforeOverlay = {};
  auto settings = engine.GetSettings();
  settings.enableForcedSubtitles = false;
  engine.applySettings(settings, false);
  require(frame() == 0 && forced.activePath().empty(),
          "Disabling forced subtitles removes them and clears the active ASS path");
  settings.enableForcedSubtitles = true;
  engine.applySettings(settings, false);
  require(frame() > 100, "Enabling forced subtitles reloads the current node");
  runtime.SetState(GameState::Menu);
  require(frame() == 0, "Forced subtitles are cleared outside gameplay");
  runtime.SetState(GameState::Gameplay);

  // A Standard runtime's global diagnostic clock is not its scene-entry clock.
  runtime.node = "80005:66";
  runtime.timedClock = false;
  runtime.seconds = 123;
  engine.resetNodeClock();
  const auto nativePath = forced.dumpForActiveNode();
  require(nativePath.filename() == "80005-66.ass",
          "Standard native dump includes both event and position");
  writeTextFileAtomically(nativePath.string(), script);
  engine.setAgentPaused(false);
  engine.synchronizeTimeline();
  SDL_Delay(1100);
  engine.synchronizeTimeline();
  const auto elapsed = engine.activeNodeSeconds();
  require(elapsed >= 1 && elapsed < 2,
          "Standard native clock counts scene-entry time, not a global clock");
  engine.setAgentPaused(true);
  require(frame() > 100 && forced.getPosition() == elapsed,
          "Standard native subtitles use event-position file and scene-entry clock");
  SDL_Delay(25);
  engine.synchronizeTimeline();
  require(engine.activeNodeSeconds() == elapsed,
          "Native fallback clock freezes while paused");
  runtime.node = "80005:68";
  engine.synchronizeTimeline();
  require(engine.activeNodeSeconds() == 0,
          "Native fallback clock resets on an event ID change");

  runtime.node.clear();
  ScriptNode dialogue;
  dialogue.id = 7;
  dialogue.type = NodeType::Dialogue;
  dialogue.text = "Standard dialogue";
  TimelineScript timed;
  timed.duration = 20;
  timed.cues.push_back({10, 3, CueType::Dialogue, "Future dialogue"});
  manager.loadScript({dialogue, ScriptNode(8, timed)});
  manager.startScript(7);
  const auto standardPath = forced.dumpForActiveNode();
  require(standardPath.filename() == "7.ass",
          "Standard VN nodes can dump ASS without timeline cues");
  writeTextFileAtomically(standardPath.string(), script);
  forced.reload();
  engine.setAgentPaused(false);
  engine.synchronizeTimeline();
  SDL_Delay(1100);
  engine.synchronizeTimeline();
  require(engine.activeNodeSeconds() >= 0.02 &&
              engine.GetDebug().nodeClockText().find("Clock: ") !=
                  std::string::npos,
          "Standard VN nodes expose an advancing HUD clock");
  engine.setAgentPaused(true);
  require(frame() > 100 &&
              forced.getPosition() == engine.activeNodeSeconds(),
          "Standard VN forced subtitles render using the shared node clock");
  manager.jumpToNode(7);
  require(engine.activeNodeSeconds() == 0,
          "Re-entering the same standard node resets its clock");

  manager.jumpToNode(8);
  const auto timedPath = forced.dumpForActiveNode();
  writeTextFileAtomically(timedPath.string(), script);
  forced.reload();
  timeline.seek(12);
  require(GetVNTextBoxRuntime(engine, "", "", 30).isVisible(),
          "Seeking forward activates the matching dialogue");
  timeline.seek(1.5);
  require(!GetVNTextBoxRuntime(engine, "", "", 30).isVisible(),
          "Seeking backwards clears dialogue from a future cue");
  rejects([&] { timeline.seek(-1); }, "Invalid timeline seeks are explicit");
  require(frame() > 100 && forced.getPosition() == 1.5,
          "Timeline without normal subtitle cues still renders forced subtitles");
  auto save = manager.captureSave();
  timeline.seek(2);
  require(frame() == 0, "Timeline seek expires forced subtitles");
  manager.restoreSave(save);
  require(frame() > 100 && forced.getPosition() == 1.5,
          "Restoring a timed save restores the forced subtitle clock");
  timeline.seek(0);
  require(frame() == 0, "Backward timeline seek hides not-yet-started text");
  manager.jumpToNode(-1);
  require(frame() == 0, "Finished scripts clear forced subtitles");
  runtime.inspectScreen = {};
  runtime.opaqueGameplay = false;
  engine.setAgentPaused(false);
}

void subtitleRasterization(Engine &engine) {
  auto &renderer = engine.GetRenderer();
  TexturePtr target(SDL_CreateTexture(&renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_TARGET, 2560, 1600));
  require(bool(target), "Create Retina-sized subtitle test target");
  require(SDL_SetRenderTarget(&renderer, target.get()),
          "Select subtitle test target");
  SubtitleScene scene(engine, 640, 360);
  scene.setStyle(AssStyle::parse(styleFile()));
  scene.setCues({{0, 5, "RETINA GLYPHS", "Person"}});
  const auto capture = [&] {
    SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255);
    SDL_RenderClear(&renderer);
    scene.render(renderer, 1);
    SurfacePtr pixels(SDL_RenderReadPixels(&renderer, nullptr));
    require(bool(pixels), "Read Retina subtitle pixels");
    return pixels;
  };
  const auto equalPixels = [](const SDL_Surface &a, const SDL_Surface &b) {
    if (a.w != b.w || a.h != b.h || a.format != b.format)
      return false;
    for (int y = 0; y < a.h; ++y)
      if (std::memcmp(static_cast<const Uint8 *>(a.pixels) + y * a.pitch,
                      static_cast<const Uint8 *>(b.pixels) + y * b.pitch,
                      a.w * SDL_BYTESPERPIXEL(a.format)) != 0)
        return false;
    return true;
  };
  require(SDL_SetRenderLogicalPresentation(
              &renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
              SDL_SetRenderScale(&renderer, 4, 4),
          "Configure a native physical-pixel reference");
  const SDL_Rect viewport{0, 20, 640, 360};
  require(SDL_SetRenderViewport(&renderer, &viewport),
          "Configure letterboxed reference viewport");
  const auto reference = capture();
  require(colored(renderer, 1) > 1000, "Reference has visible high-density text");
  require(SDL_SetRenderViewport(&renderer, nullptr) &&
              SDL_SetRenderScale(&renderer, 2, 2) &&
              SDL_SetRenderLogicalPresentation(
                  &renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX),
          "Simulate Retina output with a smaller logical scene");
  const auto retina = capture();
  require(equalPixels(*reference, *retina),
          "Logical presentation rasterizes exactly like native physical pixels");

  require(SDL_SetRenderLogicalPresentation(
              &renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
              SDL_SetRenderScale(&renderer, 4, 1600.0f / 360),
          "Configure stretched physical-pixel reference");
  const auto stretchedReference = capture();
  require(SDL_SetRenderScale(&renderer, 2, 2) &&
              SDL_SetRenderLogicalPresentation(
                  &renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_STRETCH),
          "Configure nonuniform logical presentation");
  const auto stretched = capture();
  require(equalPixels(*stretchedReference, *stretched),
          "Subtitle rasterization accounts for both physical presentation axes");
  require(SDL_SetRenderTarget(&renderer, nullptr),
          "Restore window after subtitle rasterization tests");
}

void controls(Engine &engine, TestRuntime &runtime) {
  auto settings = engine.GetSettings();
  settings.debugUIScale = 2;
  settings.masterVolume = 0.35f;
  engine.applySettings(settings, false);
  require(engine.GetAudio().getMasterVolume() == 0.35f,
          "Engine applies master volume");
  const float previousDensity = engine.GetUI().pixelDensity();
  engine.GetUI().setPixelDensity(1);
  SDL_SetRenderLogicalPresentation(&engine.GetRenderer(), 0, 0,
                                   SDL_LOGICAL_PRESENTATION_DISABLED);
  SDL_SetRenderScale(&engine.GetRenderer(), 2, 2);
  require(UI::rasterDensity(engine.GetRenderer()) == 2,
          "Font raster density includes the scene's renderer scale");
  engine.GetUI().text(engine.GetRenderer(), "High DPI", 0, 0, 400);
  require(engine.GetUI().rasterFontSize() == 24,
          "High DPI rendering preserves font metrics used by layout");
  SDL_SetRenderScale(&engine.GetRenderer(), 1, 1);
  engine.GetUI().setPixelDensity(previousDensity);
  SettingsMenu menu(engine);
  const auto press = [&](SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    menu.onEvent(event);
  };
  menu.open();
  press(SDLK_TAB); // Audio.
  press(SDLK_LEFT);
  require(engine.GetAudio().getMasterVolume() == 0.35f,
          "Volume draft does not apply early");
  press(SDLK_ESCAPE);
  menu.open();
  press(SDLK_TAB);
  press(SDLK_LEFT);
  press(SDLK_DOWN);
  press(SDLK_RETURN);
  press(SDLK_END);
  press(SDLK_RETURN);
  require(!menu.isOpen() && engine.GetAudio().getMasterVolume() == 0.30f &&
              engine.GetAudio().isMasterMuted(),
          "Generic menu applies volume and master mute");
  engine.applySettings(settings, false);
  menu.open();
  for (int i = 0; i < 6; ++i)
    press(SDLK_DOWN);
  press(SDLK_RETURN); // Scale follows backend; no shader row on software.
  press(SDLK_END);
  press(SDLK_RETURN);
  require(!menu.isOpen() &&
              engine.GetSettings().upscaling.factor ==
                  settings.upscaling.factor + .5f &&
              engine.GetSettings().upscaling.preset ==
                  settings.upscaling.preset,
          "Non-MPV settings expose scale while preserving the hidden shader "
          "selection");
  engine.applySettings(settings, false);
  engine.GetDebug().renderMenu(engine.GetRenderer());
  const float padding = ImGui::GetStyle().FramePadding.x;
  engine.GetDebug().renderMenu(engine.GetRenderer());
  require(padding >= 16 && ImGui::GetStyle().FramePadding.x == padding,
          "Debug UI enlargement is not cumulative");
  runtime.SetState(GameState::Gameplay);
  require(engine.GetDebug().nodeClockText().find("12.38s") != std::string::npos,
          "Custom node HUD shows runtime clock");
  const auto performance = engine.GetDebug().performanceText();
  require(performance.find("FPS: ") == 0 &&
              performance.find(" ms") != std::string::npos,
          "Performance HUD reports both FPS and milliseconds per frame");
}
void resolver() {
  namespace fs = std::filesystem;
  const fs::path root =
      fs::temp_directory_path() /
      ("kotonoha-resolver-" + std::to_string(SDL_GetPerformanceCounter()));
  const fs::path previous = fs::current_path();
  fs::create_directories(root / "games/resolver-test/game-assets/data");
  writeTextFileAtomically(
      (root / "games/resolver-test/game-assets/data/a.txt").string(), "a");
  fs::current_path(root);
  try {
    const auto roots = FileResolver::roots();
    require(std::find(roots.begin(), roots.end(), fs::current_path()) !=
                roots.end(),
            "Resolver searches the working directory");
    const auto asset =
        FileResolver::findGameAsset("resolver-test", "data/a.txt");
    require(asset && fs::equivalent(
                         *asset,
                         root / "games/resolver-test/game-assets/data/a.txt"),
            "Resolver finds source-tree game-assets");
    require(!FileResolver::find("does/not/exist.bin"),
            "Missing files are not found");
    bool listed = false;
    try {
      FileResolver::resolve("does/not/exist.bin");
    } catch (const std::exception &error) {
      listed = std::string(error.what()).find("does/not/exist.bin") !=
               std::string::npos;
    }
    require(listed, "resolve() reports the searched locations");
    require(fs::exists(FileResolver::font()),
            "Engine font resolves from assets/fonts");
  } catch (...) {
    fs::current_path(previous);
    fs::remove_all(root);
    throw;
  }
  fs::current_path(previous);
  fs::remove_all(root);
}
void fades(Engine &engine) {
  FadeScene::Event out{1, 3, {0, 0, 0, 255}, FadeScene::Direction::Out, true};
  require(FadeScene::alphaAt(out, 0.5) == 0 &&
              std::fabs(FadeScene::alphaAt(out, 2) - 0.5f) < 0.01f &&
              FadeScene::alphaAt(out, 4) == 1,
          "Fade-out ramps clear to opaque and holds");
  FadeScene::Event in{0, 2, {255, 255, 255, 255}, FadeScene::Direction::In};
  require(FadeScene::alphaAt(in, 0) == 1 && FadeScene::alphaAt(in, 2) == 0,
          "Fade-in ramps opaque to clear");
  FadeScene fade(engine);
  fade.add(out);
  fade.setPosition(2);
  require(fade.active() && fade.alpha() > 0.4f,
          "Timeline fade is active mid-event");
  fade.setPosition(5);
  require(!fade.active() && fade.alpha() == 1,
          "Held fade stays opaque but finished");
  fade.render(engine.GetRenderer(), 1);
}
} // namespace

int main() {
  try {
    arguments();
    resolver();
    struct StartupDirectory {
      std::filesystem::path previous = std::filesystem::current_path();
      std::filesystem::path path = std::filesystem::temp_directory_path() /
          ("kotonoha-subtitle-startup-" +
           std::to_string(SDL_GetPerformanceCounter()));
      StartupDirectory() {
        std::filesystem::create_directories(path / "game-assets/forced_subtitles");
        std::filesystem::create_directories(path / "forced_subtitles");
        std::filesystem::current_path(path);
      }
      ~StartupDirectory() {
        std::error_code error;
        std::filesystem::current_path(previous, error);
        std::filesystem::remove_all(path, error);
      }
    } startup;
    TestRuntime runtime;
    struct StyleFile {
      std::filesystem::path path =
          std::filesystem::temp_directory_path() /
          ("kotonoha-style-" + std::to_string(SDL_GetPerformanceCounter()) +
           ".ass");
      StyleFile() { writeTextFileAtomically(path.string(), styleFile()); }
      ~StyleFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
      }
    } style;
    EngineOptions options;
    options.rendererOverride = "software";
    options.subtitleStylePath = style.path.string();
    Engine engine(runtime, options);
    require(std::filesystem::equivalent(
                engine.GetForcedSubtitles().sourceDirectory(),
                startup.path / "game-assets/forced_subtitles"),
            "Startup resolves forced subtitles using overlay reference-root priority");
    require(engine.GetSubtitleStyle().has_value(),
            "Startup loads the selected style file");
    // Global interface strings share the same live dictionary as game text.
    const auto originalWidth =
        engine.GetUI().measureSized("GLOBAL UI", 1000, 24).x;
    engine.setTranslation("nodo", "GLOBAL UI", "X");
    require(engine.GetUI().measureSized("GLOBAL UI", 1000, 24).x <
                originalWidth,
            "Global UI measurement uses translated labels");
    auto dump = std::filesystem::temp_directory_path() /
                ("kotonoha-global-" +
                 std::to_string(SDL_GetPerformanceCounter()) + ".json");
    engine.GetTranslation().dump(dump.string(),
                                 {{7, {"Scene text"}}, {-1, {"Unseen UI"}}});
    Translation loaded;
    loaded.load(dump.string());
    std::filesystem::remove(dump);
    require(loaded.getSections().count("nodo") &&
                !loaded.getSections().count("-1") &&
                loaded.text(7, "GLOBAL UI") == "X" &&
                loaded.getSections().at("nodo").count("Unseen UI"),
            "Export separates global interface strings from scene routes");
    subtitles(engine);
    subtitleRasterization(engine);
    controls(engine, runtime);
    pauseSources(engine, runtime);
    forcedSubtitles(engine, runtime);
    fades(engine);
    std::cout << "Extensible arguments/help, ASS style import/rendering and "
                 "debug scaling passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Presentation tests: " << error.what() << '\n';
    return 1;
  }
}
