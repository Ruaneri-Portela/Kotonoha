#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/routing/SchoolDaysPlaybackPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace {

void Require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}

void Put(const fs::path &path, const std::string &data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << data;
  Require(bool(out), "Cannot create " + path.string());
}

void PutConfig(const fs::path &path, const std::string &logical) {
  std::vector<uint8_t> encoded;
  std::string error;
  Require(Kotonoha::SchoolDaysConfigStorage::EncodeDocument(logical, &encoded, &error),
          error);
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(encoded.data()),
            static_cast<std::streamsize>(encoded.size()));
  Require(bool(out), "Cannot create " + path.string());
}

struct TemporaryDirectory {
  fs::path path = fs::temp_directory_path() /
                  ("school-days-next-choice-" +
                   std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    Require(fs::create_directory(path), "Cannot create temporary directory");
  }
  ~TemporaryDirectory() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

Kotonoha::EngineOptions Options(
    const std::shared_ptr<SchoolDays::Assets> &assets) {
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "software";
  return options;
}

struct Application {
  SchoolDays::Runtime runtime;
  Kotonoha::Engine engine;

  Application(const std::shared_ptr<SchoolDays::Assets> &assets,
              const char *route, const fs::path &documents)
      : runtime(assets, route, documents), engine(runtime, Options(assets)) {
    runtime.SetState(Kotonoha::GameState::Gameplay);
  }
  ~Application() { runtime.shutdown(); }
};

void WriteScripts(const fs::path &root) {
  const auto script = root / "Script/English/00";
  const std::string ordinary =
      "[PrintText]=00:00:00\tFixture\tSynthetic.\t00:01:00;\n"
      "[SkipFRAME]=00:01:00;\n"
      "[Next]=00:02:00;\n";
  for (const char *name : {"00-00-A00", "00-00-A01", "00-00-A02"})
    Put(script / (std::string(name) + ".ENG.ORS"), ordinary);
  Put(script / "00-00-A03.ENG.ORS",
      "[PrintText]=00:00:00\tFixture\tChoice.\t00:02:00;\n"
      "[SkipFRAME]=00:02:00;\n"
      "[SetSELECT]=00:02:00\tFirst synthetic choice\tSecond synthetic choice\t00:03:00;\n"
      "[Next]=00:04:00;\n");
}

void EnterChoiceSceneByPlayer(SchoolDays::Runtime &runtime) {
  Require(!runtime.retailConfig().skip,
          "choice fixture must start with unread-script stopping enabled");
  Require(runtime.debugNode() == "00/00-00-A00",
          "choice fixture did not begin at A00");
  for (const char *next : {"00/00-00-A01", "00/00-00-A02",
                           "00/00-00-A03"}) {
    runtime.openPanel(4); // Native next-choice; stop at each unread script.
    Require(runtime.debugNode() == next,
            std::string("player next-choice did not reach ") + next);
    Require(runtime.debugNodeSeconds() && *runtime.debugNodeSeconds() == 0.0,
            std::string("player next-choice did not enter ") + next +
                " at tick zero");
  }
}

void TestCurrentScriptChoiceSeek(const char *route, const fs::path &root) {
  const auto documents = root / "documents-current";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  // Keep developer API assertions in an independent session: a debug jump
  // can update the read/history markers that govern the real player path.
  const auto probeDocuments = root / "documents-debug-api";
  fs::create_directories(probeDocuments);
  {
    Application probe(assets, route, probeDocuments);
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
    probe.runtime.debugJumpToNode("00/00-00-A03");
    Require(probe.runtime.debugNode() == "00/00-00-A00",
            "DIST unexpectedly accepted the developer scene jump");
#else
    probe.runtime.debugJumpToNode("00/00-00-A03");
    Require(probe.runtime.debugNode() == "00/00-00-A03",
            "DEV developer scene jump did not reach A03");
    probe.runtime.debugJumpToNode("00/00-00-A00");
    Require(probe.runtime.debugNode() == "00/00-00-A00",
            "DEV developer scene jump did not restore A00");
#endif
  }
  Application app(assets, route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  Require(app.runtime.debugNode() == "00/00-00-A03",
          "current-script fixture did not enter A03");
  Require(*app.runtime.debugNodeSeconds() == 0.0,
          "A03 did not begin at tick 0");

  app.runtime.openPanel(4);
  app.runtime.update();
  Require(app.runtime.debugNode() == "00/00-00-A03",
          "current-script next-choice changed scenes");
  Require(*app.runtime.debugNodeSeconds() == 1.0,
          "current-script next-choice did not seek one second before SkipFRAME");
}

void TestCrossScriptSkipOn(const char *route, const fs::path &root) {
  const auto documents = root / "documents-skip-on";
  fs::create_directories(documents);
  PutConfig(documents / "Config.DAT", "< Config.dat >\n[Skip]=\"1\"\n");
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  Require(app.runtime.retailConfig().skip, "Skip=1 was not loaded");
  Require(app.runtime.debugNode() == "00/00-00-A00",
          "skip-on fixture did not start at A00");

  app.runtime.openPanel(4);
  app.runtime.update();
  Require(app.runtime.debugNode() == "00/00-00-A03",
          "Skip=1 did not traverse A00-A02 to the first choice scene");
  Require(*app.runtime.debugNodeSeconds() == 1.0,
          "Skip=1 did not land one second before A03 SkipFRAME");
}

void TestUnreadStopSkipOff(const char *route, const fs::path &root) {
  const auto documents = root / "documents-skip-off";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  Require(!app.runtime.retailConfig().skip, "default Skip state is not off");
  Require(app.runtime.debugNode() == "00/00-00-A00",
          "skip-off fixture did not start at A00");

  app.runtime.openPanel(4);
  Require(app.runtime.debugNode() == "00/00-00-A01",
          "Skip=0 did not stop at the first unread destination");
  Require(*app.runtime.debugNodeSeconds() == 0.0,
          "unread destination did not stop at script start");
}

} // namespace

namespace SchoolDays {

class RuntimeChoiceTestAccess {
public:
  static void Seek(Runtime &runtime, Kotonoha_SceneTick tick) {
    runtime.seek(tick);
    Require(runtime.completePendingSeek(), "synthetic choice seek did not settle");
  }
  static void SetTick(Runtime &runtime, Kotonoha_SceneTick tick) {
    runtime.tick = tick;
    runtime.elapsed = tick / 24.0;
    runtime.updateChoiceInteraction();
  }
  static bool Handle(Runtime &runtime, const SDL_Event &event) {
    return runtime.gameScene->handleEvent(event);
  }
  static void Render(Runtime &runtime, Kotonoha::Engine &engine) {
    runtime.gameScene->render(engine.GetRenderer(), 1.0f);
  }
  static bool Pending(const Runtime &runtime) {
    return runtime.choice != nullptr && !runtime.committed;
  }
  static bool Committed(const Runtime &runtime) { return runtime.committed; }
  static int Result(const Runtime &runtime) {
    return runtime.choicePresentation.result;
  }
  static int StoredSelection(const Runtime &runtime) {
    return runtime.choiceInteraction.StoredSelection();
  }
  static bool TailActive(const Runtime &runtime) {
    return runtime.choiceTailActive();
  }
  static bool NeedsFinalFrame(const Runtime &runtime) {
    return runtime.choiceNeedsFinalFrame();
  }
  static bool FinalFrameRendered(const Runtime &runtime) {
    return runtime.choicePresentation.finalFrameRendered;
  }
  static void MarkTimelineAdvanced(Runtime &runtime) {
    (void)runtime.timeline.AdvanceTo(runtime.tick);
  }
  static Kotonoha_SceneTick ExitStart(const Runtime &runtime) {
    return runtime.choicePresentation.exitStartTick;
  }
  static bool ReadyToAdvance(const Runtime &runtime) {
    return runtime.timeline.ReadyToEnd(runtime.tick) &&
           runtime.choicePresentationComplete();
  }
  static void SetSpeed(Runtime &runtime, int index) {
    runtime.setPlaybackSpeedIndex(index);
  }
  static void AdvanceWithLongFrame(Runtime &runtime, uint64_t deltaMs) {
    runtime.lastClock = SDL_GetTicks() - deltaMs;
    runtime.update();
  }
  static Kotonoha_SceneTick CurrentTick(const Runtime &runtime) {
    return runtime.tick;
  }
  static double EffectiveSpeed(const Runtime &runtime) { return runtime.speed; }
  static int SelectedSpeed(const Runtime &runtime) { return runtime.speedIndex; }
  static bool ChoiceSpeedOverride(const Runtime &runtime) {
    return runtime.choiceSpeedOverrideActive;
  }
  static bool NarrativeMuted(Runtime &runtime) {
    return runtime.engine->GetAudio().channel("BGM")->isMuted() &&
           runtime.engine->GetAudio().channel("Voice")->isMuted() &&
           runtime.engine->GetAudio().channel("SE")->isMuted() &&
           runtime.engine->GetAudio().channel("Movie")->isMuted();
  }
  static void ClickTransport(Runtime &runtime, bool rewind) {
    Kotonoha::SchoolDaysMenuBarPresenter::Action action{};
    action.kind = rewind
                      ? Kotonoha::SchoolDaysMenuBarPresenter::ActionKind::BackOrRestartScene
                      : Kotonoha::SchoolDaysMenuBarPresenter::ActionKind::SkipToEnd;
    runtime.activateMenuAction(action);
  }
  static void PumpTransport(Runtime &runtime, uint64_t nowMs) {
    runtime.updateTransportFade(nowMs);
  }
  static Kotonoha::SchoolDaysTransportFade::Phase TransportPhase(const Runtime &runtime) {
    return runtime.transportFade.CurrentPhase();
  }
  static bool SeekPending(const Runtime &runtime) { return runtime.pendingSeek.has_value(); }
  static void Control(Runtime &runtime, int control) {
    runtime.activateControl(control);
  }
  static void MenuAction(
      Runtime &runtime,
      Kotonoha::SchoolDaysMenuBarPresenter::ActionKind kind) {
    Kotonoha::SchoolDaysMenuBarPresenter::Action action{};
    action.kind = kind;
    runtime.activateMenuAction(action);
  }
  static bool Paused(const Runtime &runtime) { return runtime.paused; }
  static bool ChoiceMenuFrozen(const Runtime &runtime) {
    return runtime.choiceMenuFrozen &&
           runtime.menuBarProjection().choiceTailFrozen;
  }
  static bool ChoiceMenuNewSceneBound(const Runtime &runtime) {
    return runtime.choiceMenuNextSceneBound;
  }
  static void SimulatePhysicalSceneRebind(Runtime &runtime,
                                          bool nextScene = true) {
    auto synthetic = runtime.bridge.CurrentScene();
    if (nextScene) synthetic.sceneKey = "00/00-00-SYNTHETIC-NEXT";
    runtime.bindScene(synthetic, runtime.bridge.CurrentGameplay());
  }
  static void ChoiceMeterCue(Runtime &runtime, int code, uint32_t nowMs) {
    runtime.noteChoiceMenuCue(code, nowMs);
  }
  static void MaybeUnlockChoiceMenu(Runtime &runtime, uint32_t nowMs,
                                    bool meterAnimating) {
    runtime.updateChoiceMenuUnlock(nowMs, meterAnimating);
  }
  static bool BarSuppressed(const Runtime &runtime) {
    return runtime.menuBarProjection().fullBarSuppressed;
  }
  static bool PromptMeterOnly(const Runtime &runtime) {
    return runtime.menuBarProjection().meterOnly;
  }
  static bool ChoiceFeelVisible(const Runtime &runtime) {
    return runtime.choiceFeelVisible;
  }
  static Kotonoha::SchoolDaysOptionalTick RawSkipTarget(const Runtime &runtime) {
    return runtime.timeline.ComputeSkipToEndTarget(runtime.tick);
  }
  static void Pause(Runtime &runtime, bool enabled) { runtime.paused = enabled; }
  static void Auto(Runtime &runtime, bool enabled) {
    runtime.config.autoMode = enabled;
    runtime.choiceInteraction.SetAutoMode(enabled);
  }
  // Deterministic simulation of an authored choice becoming visible before
  // an already started 500 ms transport fade is allowed to navigate.
  static void DispatchChoiceDuringFade(Runtime &runtime) {
    runtime.tick = 48;
    runtime.elapsed = 2.0;
    for (auto *event : runtime.timeline.AdvanceTo(48))
      runtime.dispatch(*event);
    Require(runtime.choice != nullptr && !runtime.committed,
            "fade race fixture failed to dispatch an unresolved choice");
    runtime.tick = 49;
    runtime.elapsed = 49.0 / 24.0;
    runtime.updateChoiceInteraction();
  }
  static void Control(Runtime &runtime, int control) {
    runtime.activateControl(control);
  }
  static void
  MenuAction(Runtime &runtime,
             Kotonoha::SchoolDaysMenuBarPresenter::ActionKind kind) {
    Kotonoha::SchoolDaysMenuBarPresenter::Action action{};
    action.kind = kind;
    runtime.activateMenuAction(action);
  }
  static bool Paused(const Runtime &runtime) { return runtime.paused; }
  static bool ChoiceMenuFrozen(const Runtime &runtime) {
    return runtime.choiceMenuFrozen &&
           runtime.menuBarProjection().choiceTailFrozen;
  }
  static bool ChoiceMenuNewSceneBound(const Runtime &runtime) {
    return runtime.choiceMenuNextSceneBound;
  }
  static void SimulatePhysicalSceneRebind(Runtime &runtime,
                                          bool nextScene = true) {
    auto synthetic = runtime.bridge.CurrentScene();
    if (nextScene)
      synthetic.sceneKey = "00/00-00-SYNTHETIC-NEXT";
    runtime.bindScene(synthetic, runtime.bridge.CurrentGameplay());
  }
  static void ChoiceMeterCue(Runtime &runtime, int code, uint32_t nowMs) {
    runtime.noteChoiceMenuCue(code, nowMs);
  }
  static void MaybeUnlockChoiceMenu(Runtime &runtime, uint32_t nowMs,
                                    bool meterAnimating) {
    runtime.updateChoiceMenuUnlock(nowMs, meterAnimating);
  }
  static bool BarSuppressed(const Runtime &runtime) {
    return runtime.menuBarProjection().fullBarSuppressed;
  }
  static bool PromptMeterOnly(const Runtime &runtime) {
    return runtime.menuBarProjection().meterOnly;
  }
  static bool ChoiceFeelVisible(const Runtime &runtime) {
    return runtime.choiceFeelVisible;
  }
  static Kotonoha::SchoolDaysOptionalTick
  RawSkipTarget(const Runtime &runtime) {
    return runtime.timeline.ComputeSkipToEndTarget(runtime.tick);
  }
  static void Pause(Runtime &runtime, bool enabled) {
    runtime.paused = enabled;
  }
  static void Auto(Runtime &runtime, bool enabled) {
    runtime.config.autoMode = enabled;
    runtime.choiceInteraction.SetAutoMode(enabled);
  }
  // Deterministic simulation of an authored choice becoming visible before
  // an already started 500 ms transport fade is allowed to navigate.
  static void DispatchChoiceDuringFade(Runtime &runtime) {
    runtime.tick = 48;
    runtime.elapsed = 2.0;
    for (auto *event : runtime.timeline.AdvanceTo(48))
      runtime.dispatch(*event);
    Require(runtime.choice != nullptr && !runtime.committed,
            "fade race fixture failed to dispatch an unresolved choice");
    runtime.tick = 49;
    runtime.elapsed = 49.0 / 24.0;
    runtime.updateChoiceInteraction();
  }
};

} // namespace SchoolDays

namespace {

SDL_Event MouseMotion(float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.x = x;
  event.motion.y = y;
  return event;
}

SDL_Event LeftDown(float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  return event;
}

SDL_Event KeyDown(SDL_Keycode key) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = key;
  event.key.repeat = false;
  return event;
}

SDL_Event GamepadDown() {
  SDL_Event event{};
  event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
  return event;
}

void TestRetailChoiceSelectedTail(const char *route, const fs::path &root) {
  const auto documents = root / "documents-choice-selected";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  using Access = SchoolDays::RuntimeChoiceTestAccess;

  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Require(Access::Pending(app.runtime),
          "retail SetSELECT did not open at authored start tick");

  // Choice input is mouse-only. Numeric keyboard input must not resolve it.
  app.runtime.onEvent(KeyDown(SDLK_1));
  Require(Access::Pending(app.runtime),
          "keyboard input resolved a School Days retail choice");
  app.runtime.onEvent(GamepadDown());
  Require(Access::Pending(app.runtime),
          "gamepad input resolved a School Days retail choice");

  Require(Access::Handle(app.runtime, MouseMotion(640.0f, 540.0f)) &&
              Access::StoredSelection(app.runtime) == 1,
          "bottom-half motion did not store TopBottom choice one");
  Require(Access::Handle(app.runtime, LeftDown(640.0f, 180.0f)) &&
              Access::Pending(app.runtime) &&
              Access::StoredSelection(app.runtime) == 1,
          "stale button-down incorrectly replaced the stored selection");
  Require(Access::Handle(app.runtime, LeftDown(640.0f, 540.0f)) &&
              Access::Committed(app.runtime) && Access::Result(app.runtime) == 1,
          "matching stored-selection click did not commit choice one");

  const auto start = Access::ExitStart(app.runtime);
  Access::SetTick(app.runtime, start + 47);
  Require(Access::TailActive(app.runtime) &&
              !Access::NeedsFinalFrame(app.runtime),
          "selected choice did not retain its 48-tick presentation tail");
  Access::SetTick(app.runtime, start + 48);
  Require(!Access::TailActive(app.runtime) &&
              Access::NeedsFinalFrame(app.runtime) &&
              !Access::FinalFrameRendered(app.runtime),
          "selected tail did not request its alpha-zero frame at 48 ticks");
  // Move beyond the authored scene end without rendering the tail terminator:
  // semantic routing must still be held by presentation ownership.
  Access::SetTick(app.runtime, start + 72);
  Access::MarkTimelineAdvanced(app.runtime);
  Require(Access::NeedsFinalFrame(app.runtime) &&
              !Access::ReadyToAdvance(app.runtime),
          "scene became advanceable before selected alpha-zero final frame");
  Access::Render(app.runtime, app.engine);
  Require(Access::FinalFrameRendered(app.runtime) &&
              Access::ReadyToAdvance(app.runtime),
          "selected alpha-zero final frame did not release scene advance");
}

void TestRetailChoiceTimeoutTail(const char *route, const fs::path &root) {
  const auto documents = root / "documents-choice-timeout";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  using Access = SchoolDays::RuntimeChoiceTestAccess;

  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Access::SetTick(app.runtime, 70);
  Require(Access::Pending(app.runtime),
          "choice timed out before endTick-1");
  Access::SetTick(app.runtime, 71);
  Require(Access::Committed(app.runtime) && Access::Result(app.runtime) == -1 &&
              Access::ExitStart(app.runtime) == 71,
          "choice timeout did not commit native -1 exactly at endTick-1");
  Require(Access::ChoiceMenuFrozen(app.runtime) &&
              !Access::ChoiceFeelVisible(app.runtime) &&
              !Access::PromptMeterOnly(app.runtime),
          "timeout/omission displayed the post-selection Feel bar");
  // A route may still invoke _SetFeeling for omission. A test-only
  // directional event must never grant omission the confirmed-choice overlay.
  Access::ChoiceMeterCue(app.runtime, 3, 200u);
  Require(!Access::ChoiceFeelVisible(app.runtime) &&
              !Access::PromptMeterOnly(app.runtime),
          "omitted choice projected Feel solely because a route rule ran");

  Access::SetTick(app.runtime, 94);
  Require(Access::TailActive(app.runtime),
          "timeout/nonselected presentation tail ended before 24 ticks");
  Access::SetTick(app.runtime, 95);
  Require(!Access::TailActive(app.runtime) &&
              Access::NeedsFinalFrame(app.runtime) &&
              !Access::FinalFrameRendered(app.runtime),
          "timeout/nonselected tail did not request its alpha-zero frame at 24 ticks");
  Access::Render(app.runtime, app.engine);
  Require(Access::FinalFrameRendered(app.runtime),
          "timeout alpha-zero final frame was not recorded as rendered");
}

void TestTurboStopsAtChoiceAndRestores(const char *route, const fs::path &root) {
  const auto documents = root / "documents-turbo-choice";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  using Access = SchoolDays::RuntimeChoiceTestAccess;

  EnterChoiceSceneByPlayer(app.runtime);
  Access::SetSpeed(app.runtime, 4); // UI x32 maps to internal 24x.
  Require(Access::SelectedSpeed(app.runtime) == 4 &&
              Access::EffectiveSpeed(app.runtime) == 24.0 &&
              Access::NarrativeMuted(app.runtime),
          "turbo did not initially mute high-speed narrative audio");
  Access::Seek(app.runtime, 48);
  Require(Access::Pending(app.runtime) &&
              Access::SelectedSpeed(app.runtime) == 4 &&
              Access::ChoiceSpeedOverride(app.runtime) &&
              Access::EffectiveSpeed(app.runtime) == 1.0 &&
              !Access::NarrativeMuted(app.runtime),
          "choice did not restore effective 1x/audio while retaining selected turbo chip");
  Require(Access::Handle(app.runtime, MouseMotion(640.0f, 540.0f)) &&
              Access::Handle(app.runtime, LeftDown(640.0f, 540.0f)) &&
              Access::Committed(app.runtime) && Access::EffectiveSpeed(app.runtime) == 1.0,
          "choice selection did not keep 1x through the original 48-tick exit tail");
  // Simulate the original scene-swap boundary without resetting the user's
  // selected multiplier: a rewind to a new scene still restores that state.
  Access::Seek(app.runtime, 0);
  Require(!Access::ChoiceSpeedOverride(app.runtime) &&
              Access::SelectedSpeed(app.runtime) == 4 &&
              Access::EffectiveSpeed(app.runtime) == 24.0 &&
              Access::NarrativeMuted(app.runtime),
          "choice exit did not restore the originally selected fast-forward speed/audio");
}

void TestTurboCannotCrossChoiceWithLongFrame(const char *route, const fs::path &root) {
  const auto documents = root / "documents-turbo-longframe";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  EnterChoiceSceneByPlayer(app.runtime);
  Access::SetSpeed(app.runtime, 4);
  // An oversized update used to seek straight across the entire 48..72
  // SetSELECT. Its clamped boundary must be 48, not the authored timeout.
  Access::AdvanceWithLongFrame(app.runtime, 2000u);
  Require(Access::Pending(app.runtime) &&
              Access::CurrentTick(app.runtime) == 48 &&
              Access::EffectiveSpeed(app.runtime) == 1.0 &&
              Access::SelectedSpeed(app.runtime) == 4,
          "high speed or a long frame skipped over the first pending choice");
}

void TestTransportFadeBeforeSeek(const char *route, const fs::path &root) {
  const auto documents = root / "documents-transport-fades";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  using Fade = Kotonoha::SchoolDaysTransportFade;
  EnterChoiceSceneByPlayer(app.runtime);

  const uint64_t skipAt = SDL_GetTicks();
  Access::ClickTransport(app.runtime, false);
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::FadeOut &&
              !Access::SeekPending(app.runtime),
          "SkipToEnd jumped abruptly instead of starting the retail black fade");
  Access::PumpTransport(app.runtime, skipAt + 250u);
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::FadeOut &&
              Access::CurrentTick(app.runtime) == 0 &&
              !Access::SeekPending(app.runtime),
          "SkipToEnd moved before the 500ms outgoing fade finished");
  Access::PumpTransport(app.runtime, skipAt + 550u);
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::AwaitReady &&
              Access::SeekPending(app.runtime),
          "SkipToEnd did not seek behind its opaque black frame");
  app.runtime.update();
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::FadeIn &&
              !Access::SeekPending(app.runtime) &&
              Access::CurrentTick(app.runtime) >= 24,
          "SkipToEnd did not reveal its fully resolved destination");
  Access::PumpTransport(app.runtime, SDL_GetTicks() + 1500u);
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::Idle,
          "SkipToEnd fade did not unlock the following transport action");

  // Rewind uses the same state4 fade but targets the current scene start.
  // Restart the existing physical script without a developer scene jump.
  Access::Seek(app.runtime, 0);
  Require(app.runtime.debugNode() == "00/00-00-A03" &&
              Access::CurrentTick(app.runtime) == 0,
          "transport fixture did not restore A03 to its first frame");
  Access::Seek(app.runtime, 24);
  const uint64_t rewindAt = SDL_GetTicks();
  Access::ClickTransport(app.runtime, true);
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::FadeOut &&
              Access::CurrentTick(app.runtime) == 24,
          "Rewind jumped to tick 0 before beginning its fade");
  Access::PumpTransport(app.runtime, rewindAt + 550u);
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::AwaitReady &&
              Access::SeekPending(app.runtime),
          "Rewind did not navigate under the black frame");
  app.runtime.update();
  Require(Access::TransportPhase(app.runtime) == Fade::Phase::FadeIn &&
              Access::CurrentTick(app.runtime) == 0,
          "Rewind did not reveal the scene start after its seek settled");
}

void TestSkipChoiceVisible(const char *route, const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  const auto documents = root / "documents-skip-visible";
  fs::create_directories(documents);
  Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Access::SetTick(app.runtime, 49);
  Require(Access::Pending(app.runtime), "visible choice fixture is not pending");
  Require(Access::BarSuppressed(app.runtime),
          "T1: the top bar was not suppressed while the prompt was visible");
  Require(!Access::PromptMeterOnly(app.runtime) &&
              !Access::ChoiceFeelVisible(app.runtime),
          "T1: an unanswered choice showed the post-selection Feel meter");
  Access::ClickTransport(app.runtime, false);
  Require(Access::TransportPhase(app.runtime) ==
              Kotonoha::SchoolDaysTransportFade::Phase::Idle &&
              !Access::SeekPending(app.runtime) &&
              Access::CurrentTick(app.runtime) == 49 &&
              Access::Pending(app.runtime) && Access::Result(app.runtime) == -2,
          "T1: clicking SkipToEnd on the prompt started a fade or changed the choice");
}

void TestSelectedChoiceTailDoesNotAcceptMenuActions(const char *route,
                                                     const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  using Kind = Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
  const auto documents = root / "documents-choice-frozen-tail";
  fs::create_directories(documents);
  Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Require(Access::Pending(app.runtime), "tail fixture has no pending choice");
  Require(Access::Handle(app.runtime, MouseMotion(640.0f, 540.0f)) &&
              Access::Handle(app.runtime, LeftDown(640.0f, 540.0f)) &&
              Access::Committed(app.runtime),
          "tail fixture player choice was not committed");
  Require(!Access::BarSuppressed(app.runtime),
          "selected prompt must restore MenuBar chrome ownership");
  Require(!Access::PromptMeterOnly(app.runtime),
          "selected choice displayed the Feel meter before any points changed");
  Require(!Access::ChoiceFeelVisible(app.runtime),
          "clicking choice without a delta detector revealed Feel too early");
  Require(Access::ChoiceMenuFrozen(app.runtime) &&
              !Access::ChoiceMenuNewSceneBound(app.runtime),
          "selected prompt must restore visible but noninteractive menu");
  Require(!Access::Paused(app.runtime), "fixture started paused");
  Access::MenuAction(app.runtime, Kind::TogglePause);
  Require(!Access::Paused(app.runtime),
          "selected choice tail accepted the frozen MenuBar PAUSE action");
  Access::Control(app.runtime, 3);
  app.runtime.onEvent(KeyDown(SDLK_RIGHT));
  Require(!Access::SeekPending(app.runtime) &&
              Access::TransportPhase(app.runtime) ==
                  Kotonoha::SchoolDaysTransportFade::Phase::Idle,
          "selected choice tail accepted skip before the musical meter phase");

  constexpr uint32_t cueAt = 1000u;
  Access::ChoiceMeterCue(app.runtime, 3, cueAt);
  Require(Access::ChoiceFeelVisible(app.runtime) &&
              Access::PromptMeterOnly(app.runtime),
          "confirmed point change did not reveal Feel alone before next scene");
  Access::MaybeUnlockChoiceMenu(app.runtime, cueAt + 6000u, false);
  Require(Access::ChoiceMenuFrozen(app.runtime),
          "menu thawed before the next physical scene was associated");
  // Rebind an isolated, synthetic physical scene through the same runtime
  // owner switch used by narrative transitions; no retail timeline involved.
  Access::SimulatePhysicalSceneRebind(app.runtime, false);
  Require(!Access::ChoiceMenuNewSceneBound(app.runtime) &&
              Access::ChoiceMenuFrozen(app.runtime),
          "rebinding the same current part must not thaw the bar");
  Access::SimulatePhysicalSceneRebind(app.runtime);
  Require(Access::ChoiceMenuNewSceneBound(app.runtime) &&
              Access::ChoiceMenuFrozen(app.runtime) &&
              Access::ChoiceFeelVisible(app.runtime) &&
              Access::PromptMeterOnly(app.runtime),
          "physical scene rebind exposed Top Menu chrome during changed Feel");
  Access::MaybeUnlockChoiceMenu(app.runtime, cueAt + 3924u, false);
  Require(Access::ChoiceMenuFrozen(app.runtime),
          "SeUp menu unlocked before the documented 3.924172-second cue ended");
  Access::MaybeUnlockChoiceMenu(app.runtime, cueAt + 3925u, true);
  Require(Access::ChoiceMenuFrozen(app.runtime),
          "menu unlocked before the feeling meter finished animating");
  Access::MaybeUnlockChoiceMenu(app.runtime, cueAt + 3925u, false);
  Require(!Access::ChoiceMenuFrozen(app.runtime),
          "menu failed to thaw after new scene, meter and SeUp had completed");
  Require(!Access::ChoiceFeelVisible(app.runtime) &&
              !Access::PromptMeterOnly(app.runtime),
          "completed feeling cue left standalone Feel stuck on screen");
  Access::MenuAction(app.runtime, Kind::TogglePause);
  Require(Access::Paused(app.runtime),
          "ordinary MenuBar control did not work again after the cue ended");
}

void TestChoiceMenuDownAndSilentRelease(const char *route, const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  const auto documents = root / "documents-frozen-cue-variants";
  fs::create_directories(documents);
  {
    Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
    EnterChoiceSceneByPlayer(app.runtime);
    Access::Seek(app.runtime, 48);
    Require(Access::Handle(app.runtime, MouseMotion(640.0f, 540.0f)) &&
                Access::Handle(app.runtime, LeftDown(640.0f, 540.0f)),
            "synthetic SeDown fixture did not confirm player choice");
    Access::SimulatePhysicalSceneRebind(app.runtime);
    Access::ChoiceMeterCue(app.runtime, 4, 250u);
    Require(Access::ChoiceFeelVisible(app.runtime),
            "SeDown feeling change did not trigger standalone meter");
    Access::MaybeUnlockChoiceMenu(app.runtime, 4272u, false);
    Require(Access::ChoiceMenuFrozen(app.runtime),
            "SeDown release happened before the 4.022857-second effect ended");
    Access::MaybeUnlockChoiceMenu(app.runtime, 4273u, false);
    Require(!Access::ChoiceMenuFrozen(app.runtime),
            "SeDown release did not follow the documented effect duration");
  }

  // Some confirmed choices change no feeling value. There must be no
  // invented second cue or mandatory four-second wait in that case.
  const auto silentDocuments = root / "documents-frozen-cue-silent";
  fs::create_directories(silentDocuments);
  Application silent(std::make_shared<SchoolDays::Assets>(root), route,
                     silentDocuments);
  EnterChoiceSceneByPlayer(silent.runtime);
  Access::Seek(silent.runtime, 48);
  Require(Access::Handle(silent.runtime, MouseMotion(640.0f, 540.0f)) &&
              Access::Handle(silent.runtime, LeftDown(640.0f, 540.0f)),
          "silent synthetic choice did not commit");
  Access::SimulatePhysicalSceneRebind(silent.runtime);
  Access::ChoiceMeterCue(silent.runtime, 0, 250u);
  Require(!Access::ChoiceFeelVisible(silent.runtime) &&
              !Access::PromptMeterOnly(silent.runtime),
          "unchanged confirmed choice invented a standalone Feel meter");
  Access::MaybeUnlockChoiceMenu(silent.runtime, 250u, false);
  Require(!Access::ChoiceMenuFrozen(silent.runtime),
          "no-change choice invented a second musical cue or artificial delay");
}

void TestSkipChoiceFadeRace(const char *route, const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  using Phase = Kotonoha::SchoolDaysTransportFade::Phase;
  const auto documents = root / "documents-skip-race";
  fs::create_directories(documents);
  Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  const uint64_t now = SDL_GetTicks();
  Access::ClickTransport(app.runtime, false);
  Require(Access::TransportPhase(app.runtime) == Phase::FadeOut,
          "T2: first SkipToEnd did not arm the expected fade");
  Access::DispatchChoiceDuringFade(app.runtime);
  Access::PumpTransport(app.runtime, now + 600u);
  Require(Access::TransportPhase(app.runtime) == Phase::Idle &&
              !Access::SeekPending(app.runtime) &&
              Access::CurrentTick(app.runtime) == 49 &&
              Access::Pending(app.runtime) && Access::Result(app.runtime) == -2,
          "T2: fade that crossed SetSELECT recomputed a target and sought over the prompt");
}

void TestSkipChoiceKeyboard(const char *route, const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  const auto documents = root / "documents-skip-keyboard";
  fs::create_directories(documents);
  Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Access::SetTick(app.runtime, 49);
  app.runtime.onEvent(KeyDown(SDLK_RIGHT));
  Require(!Access::SeekPending(app.runtime) && Access::Pending(app.runtime) &&
              Access::CurrentTick(app.runtime) == 49,
          "T3: RIGHT skipped over an unresolved choice");
  app.runtime.onEvent(KeyDown(SDLK_LEFT));
  Require(!Access::SeekPending(app.runtime) && Access::Pending(app.runtime) &&
              Access::CurrentTick(app.runtime) == 49,
          "T3: LEFT restarted the scene while a choice was waiting");
}

void TestSkipChoiceControlAndNext(const char *route, const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  const auto documents = root / "documents-skip-control";
  fs::create_directories(documents);
  Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Access::SetTick(app.runtime, 49);
  Access::Control(app.runtime, 3);
  Require(!Access::SeekPending(app.runtime) && Access::Pending(app.runtime),
          "T6: activateControl(3) crossed a pending choice");
  Access::Control(app.runtime, 4);
  Require(!Access::SeekPending(app.runtime) && Access::Pending(app.runtime) &&
              Access::CurrentTick(app.runtime) == 49 &&
              Access::Result(app.runtime) == -2,
          "T7: next-choice from an active prompt resolved or bypassed it");
}

void TestSkipChoiceRapidAndSettings(const char *route, const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  using Phase = Kotonoha::SchoolDaysTransportFade::Phase;
  const auto documents = root / "documents-skip-rapid";
  fs::create_directories(documents);
  Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Access::SetTick(app.runtime, 49);
  for (const int count : {2, 3, 10, 50}) {
    for (int i = 0; i < count; ++i) {
      Access::Pause(app.runtime, (i % 2) == 0);
      Access::Auto(app.runtime, (i % 3) == 0);
      Access::SetSpeed(app.runtime, (i % 2) ? 1 : 4); // UI x2 / x32
      Access::ClickTransport(app.runtime, false);
      Access::PumpTransport(app.runtime,
                            SDL_GetTicks() + 500u + uint64_t(i % 5) * 113u);
      Require(Access::TransportPhase(app.runtime) == Phase::Idle &&
                  !Access::SeekPending(app.runtime) &&
                  Access::CurrentTick(app.runtime) == 49 &&
                  Access::Pending(app.runtime) && Access::Result(app.runtime) == -2,
              "T4/T5: repeated skip under AUTO, pause or x2/x32 changed the choice");
    }
  }
}

void TestSkipChoicePostCommitAndNoChoice(const char *route,
                                         const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  {
    const auto documents = root / "documents-skip-post-choice";
    fs::create_directories(documents);
    Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
    EnterChoiceSceneByPlayer(app.runtime);
    Access::Seek(app.runtime, 48);
    Require(Access::Handle(app.runtime, MouseMotion(640.0f, 540.0f)) &&
                Access::Handle(app.runtime, LeftDown(640.0f, 540.0f)) &&
                Access::Committed(app.runtime) && Access::Result(app.runtime) == 1,
            "T8: legitimate player selection failed");
    Access::Control(app.runtime, 3);
    Require(!Access::SeekPending(app.runtime) &&
                Access::ChoiceMenuFrozen(app.runtime),
            "T8: a confirmed choice activated skip before the feeling tail ended");
    Access::SimulatePhysicalSceneRebind(app.runtime);
    Access::MaybeUnlockChoiceMenu(app.runtime, 0u, false);
    Require(!Access::ChoiceMenuFrozen(app.runtime),
            "T8: no-effect scene transition did not release the menu");
    Access::Control(app.runtime, 3);
    Require(Access::SeekPending(app.runtime),
            "T8: skip did not resume after the next scene's soundless transition");
  }
  {
    const auto documents = root / "documents-skip-no-choice";
    fs::create_directories(documents);
    Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
    // The launcher has not yet bound A00's physical timeline. Enter the
    // next explicitly authored, still choice-free physical scene first.
    Access::Control(app.runtime, 4);
    Require(app.runtime.debugNode() == "00/00-00-A01",
            "T9: no-choice fixture did not enter A01 by the player route");
    const auto originalTarget = Access::RawSkipTarget(app.runtime);
    Require(originalTarget.has_value(),
            "T9: ordinary scene must expose an authored skip target");
    Access::Control(app.runtime, 3);
    Require(Access::SeekPending(app.runtime),
            "T9: scene without a choice lost its ordinary SkipToEnd at tick " +
                std::to_string(Access::CurrentTick(app.runtime)) +
                " expected destination " + std::to_string(*originalTarget));
    Require(app.runtime.debugNodeSeconds() &&
                *app.runtime.debugNodeSeconds() == 0.0,
            "T9: seek must still be pending until the runtime processes it");
  }
}

void TestSkipChoiceRandomSequences(const char *route, const fs::path &root) {
  using Access = SchoolDays::RuntimeChoiceTestAccess;
  const auto documents = root / "documents-skip-random";
  fs::create_directories(documents);
  Application app(std::make_shared<SchoolDays::Assets>(root), route, documents);
  EnterChoiceSceneByPlayer(app.runtime);
  Access::Seek(app.runtime, 48);
  Access::SetTick(app.runtime, 49);
  uint32_t seed = 0x5EEDBEEFu;
  const auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed;
  };
  for (int sequence = 0; sequence < 256; ++sequence) {
    for (int step = 0; step < 12; ++step) {
      switch (next() % 9u) {
      case 0: Access::ClickTransport(app.runtime, false); break;
      case 1: app.runtime.onEvent(KeyDown(SDLK_RIGHT)); break;
      case 2: app.runtime.onEvent(KeyDown(SDLK_LEFT)); break;
      case 3: Access::Control(app.runtime, 3); break;
      case 4: Access::Control(app.runtime, 4); break;
      case 5: app.runtime.onEvent(GamepadDown()); break;
      case 6: Access::Auto(app.runtime, (next() & 1u) != 0); break;
      case 7: Access::Pause(app.runtime, (next() & 1u) != 0); break;
      case 8: Access::SetSpeed(app.runtime, (next() & 1u) != 0 ? 1 : 4); break;
      }
      Access::PumpTransport(app.runtime, SDL_GetTicks() + 750u);
      Require(Access::Pending(app.runtime) && !Access::Committed(app.runtime) &&
                  Access::Result(app.runtime) == -2 &&
                  Access::CurrentTick(app.runtime) == 49 &&
                  !Access::SeekPending(app.runtime) &&
                  Access::TransportPhase(app.runtime) ==
                      Kotonoha::SchoolDaysTransportFade::Phase::Idle,
              "randomized skip sequence crossed or resolved an uncommitted choice; sequence=" +
                  std::to_string(sequence) + " step=" + std::to_string(step));
    }
  }
  std::cout << "SkipChoiceRandomSequences: 256 sequences x 12 steps, seed=0x5EEDBEEF PASS\n";
}

int RunBarrierCase(const char *name, void (*test)(const char*, const fs::path&),
                   const char *route, const fs::path &root) {
  try {
    test(route, root);
    std::cout << "[skip-choice] " << name << ": PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[skip-choice] " << name << ": FAIL: " << error.what() << '\n';
    return 1;
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    Require(argc == 2, "Usage: SchoolDaysNextChoiceRuntimeTest route");
    TemporaryDirectory temporary;
    WriteScripts(temporary.path);
    TestCurrentScriptChoiceSeek(argv[1], temporary.path);
    TestCrossScriptSkipOn(argv[1], temporary.path);
    TestUnreadStopSkipOff(argv[1], temporary.path);
    TestRetailChoiceSelectedTail(argv[1], temporary.path);
    TestRetailChoiceTimeoutTail(argv[1], temporary.path);
    TestTurboStopsAtChoiceAndRestores(argv[1], temporary.path);
    TestTurboCannotCrossChoiceWithLongFrame(argv[1], temporary.path);
    TestTransportFadeBeforeSeek(argv[1], temporary.path);
    int failures = 0;
    failures += RunBarrierCase("T1 menu visible", TestSkipChoiceVisible, argv[1], temporary.path);
    failures += RunBarrierCase("T12 frozen post-choice menu",
                               TestSelectedChoiceTailDoesNotAcceptMenuActions,
                               argv[1], temporary.path);
    failures += RunBarrierCase("T13 SeDown and silent choice",
                               TestChoiceMenuDownAndSilentRelease,
                               argv[1], temporary.path);
    failures += RunBarrierCase("T2 fade race", TestSkipChoiceFadeRace, argv[1], temporary.path);
    failures += RunBarrierCase("T3 arrows", TestSkipChoiceKeyboard, argv[1], temporary.path);
    failures += RunBarrierCase("T4/T5 rapid/settings", TestSkipChoiceRapidAndSettings, argv[1], temporary.path);
    failures += RunBarrierCase("T6/T7 controls", TestSkipChoiceControlAndNext, argv[1], temporary.path);
    failures += RunBarrierCase("T8/T9 postcommit/no-choice", TestSkipChoicePostCommitAndNoChoice,
                               argv[1], temporary.path);
    failures += RunBarrierCase("T1-T7 randomized", TestSkipChoiceRandomSequences,
                               argv[1], temporary.path);
    Require(failures == 0, std::to_string(failures) + " skip-choice regression groups failed");
    std::cout << "SchoolDaysNextChoiceRuntimeTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysNextChoiceRuntimeTest: " << error.what() << '\n';
    return 1;
  }
}
