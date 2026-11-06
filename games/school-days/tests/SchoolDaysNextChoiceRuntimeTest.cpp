#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/routing/SchoolDaysPlaybackPolicy.hpp>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
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

void TestCurrentScriptChoiceSeek(const char *route, const fs::path &root) {
  const auto documents = root / "documents-current";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  app.runtime.debugJumpToNode("00/00-00-A03");
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

  app.runtime.debugJumpToNode("00/00-00-A03");
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

  app.runtime.debugJumpToNode("00/00-00-A03");
  Access::Seek(app.runtime, 48);
  Access::SetTick(app.runtime, 70);
  Require(Access::Pending(app.runtime),
          "choice timed out before endTick-1");
  Access::SetTick(app.runtime, 71);
  Require(Access::Committed(app.runtime) && Access::Result(app.runtime) == -1 &&
              Access::ExitStart(app.runtime) == 71,
          "choice timeout did not commit native -1 exactly at endTick-1");

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

  app.runtime.debugJumpToNode("00/00-00-A03");
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
  app.runtime.debugJumpToNode("00/00-00-A03");
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
  app.runtime.debugJumpToNode("00/00-00-A03");

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
  app.runtime.debugJumpToNode("00/00-00-A03");
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
    std::cout << "SchoolDaysNextChoiceRuntimeTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysNextChoiceRuntimeTest: " << error.what() << '\n';
    return 1;
  }
}
