#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/routing/SchoolDaysConfigStorage.hpp>
#include <Kotonoha/routing/SchoolDaysMenuBarPresenter.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void Check(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

bool Near(double a, double b) { return std::abs(a - b) < 0.000001; }

struct TemporaryDirectory {
  fs::path path = fs::temp_directory_path() /
                  ("school-days-menubar-runtime-" +
                   std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    Check(fs::create_directory(path), "cannot create temporary directory");
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
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

void CheckMeterSfxHostCodes(Application &app, const fs::path &scratchRoot) {
  // The meter presenter only loads the two PNG textures on Open; synthetic
  // tiny, valid RGBA PNGs suffice to test the actual update/consume boundary
  // without retail packs, GUI clicks or write access to user profiles.
  constexpr unsigned char kTwoPixelPng[] = {
      0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
      0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,
      0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,
      0x15,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xfc,0xff,0xff,0xff,
      0x7f,0x06,0x06,0x06,0x06,0x26,0x06,0x28,0x00,0x00,0x3d,0xf8,
      0x04,0x00,0xda,0x3b,0x2a,0x9c,0x00,0x00,0x00,0x00,0x49,0x45,
      0x4e,0x44,0xae,0x42,0x60,0x82};
  const fs::path pictureRoot = scratchRoot / "meter-sfx";
  const fs::path pictureDir = pictureRoot / "System" / "MenuBar";
  fs::create_directories(pictureDir);
  // The extracted-assets provider requires a Script/English root even when
  // this small test only exercises the MenuBar art. No scripts are executed.
  fs::create_directories(pictureRoot / "Script" / "English");
  for (const char *name : {"MenuBar.png", "MenuBar_Chip.png"}) {
    std::ofstream out(pictureDir / name, std::ios::binary);
    out.write(reinterpret_cast<const char *>(kTwoPixelPng), sizeof(kTwoPixelPng));
    Check(static_cast<bool>(out), "could not create isolated meter PNG fixture");
  }
  auto sprites = std::make_shared<SchoolDays::Assets>(pictureRoot);
  Kotonoha::SchoolDaysMenuBarPresenter meter;
  std::string error;
  using Variant = Kotonoha::SchoolDaysMenuBarPresenter::Variant;
  const auto open = [&](int one, int two) {
    error.clear();
    Check(meter.Open(&app.engine.GetRenderer(), sprites.get(),
                     pictureRoot.string(), Variant::WideFull, one, two,
                     &error), "meter fixture failed to open: " + error);
    Check(meter.ConsumeFeelingDirectionCode() == 0,
          "opening meter synthesized a post-choice musical cue");
  };

  open(10, 20);
  meter.RequestFeelingRefresh();
  meter.Update(100, 11, 20); // capture does not yet produce a sound
  Check(meter.ConsumeFeelingDirectionCode() == 0,
        "meter played the post-choice cue before detection");
  meter.Update(101, 11, 20); // first changed value 001 increased
  Check(meter.ConsumeFeelingDirectionCode() == 3 &&
            meter.ConsumeFeelingDirectionCode() == 0,
        "increased feeling did not produce exactly one SeUp host code");
  meter.Update(102, 11, 20);
  Check(meter.ConsumeFeelingDirectionCode() == 0,
        "interpolation repeated the SeUp cue without a new detection");

  open(11, 20);
  meter.RequestFeelingRefresh();
  meter.Update(200, 11, 19);
  meter.Update(201, 11, 19);
  Check(meter.ConsumeFeelingDirectionCode() == 4 &&
            meter.ConsumeFeelingDirectionCode() == 0,
        "decreased 002 feeling did not dispatch SeDown once");

  open(5, 10);
  meter.RequestFeelingRefresh();
  meter.Update(300, 6, 9);
  meter.Update(301, 6, 9);
  Check(meter.ConsumeFeelingDirectionCode() == 3,
        "simultaneous opposite-sign changes did not prefer 001 for SeUp");

  open(20, 20);
  meter.RequestFeelingRefresh();
  meter.Update(400, 20, 20);
  meter.Update(401, 20, 20);
  Check(meter.ConsumeFeelingDirectionCode() == 0,
        "unchanged feelings incorrectly triggered the musical cue");

  // A late-game save may validly saturate the green bar. Loading another
  // (neutral) save without exiting must replace its visual presentation even
  // though no normal KTRF writer notification is raised by SaveFile restore.
  open(-200, 200);
  Check(meter.Meter().CurrentVisual().primary == -1000.0f &&
            meter.Meter().CurrentVisual().opposite == 1000.0f,
        "saturated ending save fixture did not initialize the original meter");
  meter.RequestFeelingRefresh();
  meter.Update(500, -199, 200);
  meter.Update(501, -199, 200);
  Check(meter.ConsumeFeelingDirectionCode() == 3,
        "meter fixture could not enter changed feeling state");
  meter.RequestFeelingRefresh();
  meter.Update(502, -198, 200);
  meter.RestoreFeelingState(20, 20);
  Check(meter.IsOpen() && meter.Meter().CurrentVisual().primary == 0.0f &&
            meter.Meter().CurrentVisual().opposite == 0.0f &&
            meter.Meter().CurrentPhase() ==
                Kotonoha::SchoolDaysTopMeterModel::Phase::Capture &&
            !meter.FeelingLatch() && meter.ConsumeFeelingDirectionCode() == 0,
        "LOAD inherited the previous ending's saturated bar or pending cue");
  meter.Update(503, 20, 20);
  Check(meter.Meter().CurrentVisual().primary == 0.0f &&
            meter.ConsumeFeelingDirectionCode() == 0,
        "LOAD's next frame re-applied stale ending feelings");
  meter.RestoreFeelingState(120, 0);
  Check(meter.Meter().CurrentVisual().primary == 300.0f &&
            meter.Meter().CurrentVisual().opposite == -300.0f,
        "loading another valid high-feeling save improperly normalized values");
  meter.RestoreFeelingState(0, 0);
  Check(meter.Meter().CurrentVisual().primary == 0.0f &&
            meter.ConsumeFeelingDirectionCode() == 0,
        "starting normal gameplay after a final inherited stale meter state");
  meter.Close();
}

void CheckMeterCompositingPixels(Application &app,
                                 const fs::path &scratchRoot) {
  // Authored, fully opaque synthetic PNGs: red MenuBar background and cyan
  // CHIP atlas. No commercial assets are loaded. Read pixels from the actual
  // SDL software renderer to detect a future shared-alpha or render-order
  // regression that purely logical visibility assertions would miss.
  const fs::path source = fs::path(__FILE__).parent_path() /
                          "fixtures" / "menubar-alpha";
  const fs::path root = scratchRoot / "meter-composite";
  const fs::path images = root / "System" / "MenuBar";
  fs::create_directories(images);
  fs::create_directories(root / "Script" / "English");
  for (const char *name : {"MenuBar.png", "MenuBar_Chip.png"}) {
    const fs::path from = source / name;
    Check(fs::is_regular_file(from),
          "missing synthetic MenuBar alpha fixture: " + from.string());
    fs::copy_file(from, images / name);
  }

  auto sprites = std::make_shared<SchoolDays::Assets>(root);
  auto &renderer = app.engine.GetRenderer();
  using Presenter = Kotonoha::SchoolDaysMenuBarPresenter;
  Presenter menu;
  std::string error;
  Check(menu.Open(&renderer, sprites.get(), root.string(),
                  Presenter::Variant::Standard, 10, 20, &error),
        "compositing fixture failed to open: " + error);
  int outputWidth = 0, outputHeight = 0;
  SDL_RendererLogicalPresentation presentation{};
  Check(SDL_GetRenderLogicalPresentation(&renderer, &outputWidth,
                                         &outputHeight, &presentation),
        "could not query MenuBar render output");
  if (presentation == SDL_LOGICAL_PRESENTATION_DISABLED)
    Check(SDL_GetCurrentRenderOutputSize(&renderer, &outputWidth,
                                         &outputHeight),
          "could not query software output dimensions");
  const auto viewport = Presenter::FitViewport(
      outputWidth, outputHeight,
      Presenter::LayoutForVariant(Presenter::Variant::Standard));
  Check(viewport.scale > 0.0f, "compositing fixture viewport invalid");

  const auto sample = [&](float logicalX, float logicalY) {
    SDL_Surface *frame = SDL_RenderReadPixels(&renderer, nullptr);
    Check(frame != nullptr,
          std::string("SDL software pixel readback failed: ") + SDL_GetError());
    const int x = static_cast<int>(std::lround(viewport.x + logicalX *
                                              viewport.scale));
    const int y = static_cast<int>(std::lround(viewport.y + logicalY *
                                              viewport.scale));
    Uint8 r = 0, g = 0, b = 0, a = 0;
    const bool ok = SDL_ReadSurfacePixel(frame, x, y, &r, &g, &b, &a);
    SDL_DestroySurface(frame);
    Check(ok, "MenuBar readback sample was out of bounds");
    return std::array<int, 3>{{r, g, b}};
  };
  const auto paint = [&]() {
    Check(SDL_SetRenderDrawColor(&renderer, 0, 0, 0, 255) &&
              SDL_RenderClear(&renderer) && menu.Render(&renderer, &error),
          "MenuBar synthetic draw failed: " + error);
  };

  menu.SetRuntimeState(true, false, 0, false, true, false,
                       false, true); // prompt suppresses chrome and Feel
  menu.SetMeterOnly(false);
  SDL_Event hover{};
  hover.type = SDL_EVENT_MOUSE_MOTION;
  hover.motion.x = viewport.x + 200.0f * viewport.scale;
  hover.motion.y = viewport.y + 35.0f * viewport.scale;
  Presenter::Action action;
  (void)menu.HandleEvent(&renderer, &hover, &action);
  menu.Update(static_cast<uint32_t>(SDL_GetTicks()) + 350u, 10, 20);
  paint();
  const auto choiceMeter = sample(200, 5);
  const auto hiddenChrome = sample(0, 40);
  const auto promptAuto = sample(50, 10);
  Check(choiceMeter[0] == 0 && choiceMeter[1] == 0 &&
            choiceMeter[2] == 0 &&
            hiddenChrome[0] == 0 && hiddenChrome[1] == 0 &&
            hiddenChrome[2] == 0 &&
            promptAuto[0] == 5 && promptAuto[1] == 235 &&
            promptAuto[2] == 245,
        "retail A03: cursor over the pending choice's top strip exposed chrome/Feel or hid AUTO ON");

  menu.SetMeterOnly(false);
  menu.SetRuntimeState(true, false, 0, false, true, false,
                       false, false);
  menu.SetChoiceTailFrozen(true);
  menu.SetChoiceTailFeelingVisible(false); // no delta: no isolated Feel
  paint();
  const auto idleSilentMeter = sample(200, 5);
  const auto idleSilentChrome = sample(0, 40);
  const auto silentAuto = sample(50, 10);
  Check(idleSilentMeter[0] == 0 && idleSilentMeter[1] == 0 &&
            idleSilentMeter[2] == 0 &&
            idleSilentChrome[0] == 0 && idleSilentChrome[1] == 0 &&
            idleSilentChrome[2] == 0 &&
            silentAuto[0] == 5 && silentAuto[1] == 235 &&
            silentAuto[2] == 245,
        "silent confirmed choice revealed Feel/chrome or hid AUTO without hover");

  (void)menu.HandleEvent(&renderer, &hover, &action);
  const uint32_t nowMs = static_cast<uint32_t>(SDL_GetTicks());
  menu.Update(nowMs + 350u, 10, 20); // ignored while result remains frozen
  // Frozen choice must not restart the whole Top Menu even when stale mouse
  // motion or a previous hover targeted its area. The only persistent top
  // sprite is AUTO ON; an omitted/no-delta result cannot show Feel later.
  paint();
  const auto silentMeter = sample(200, 5);
  const auto silentChrome = sample(0, 40);
  const auto tailAuto = sample(50, 10);
  Check(silentMeter[0] == 0 && silentMeter[1] == 0 &&
            silentMeter[2] == 0 &&
            silentChrome[0] == 0 && silentChrome[1] == 0 &&
            silentChrome[2] == 0 &&
            tailAuto[0] == 5 && tailAuto[1] == 235 &&
            tailAuto[2] == 245,
        "unchanged/omitted choice revealed Top Menu/Feel or lost AUTO ON");

  // The meter's actual changed-feeling event triggers a second presentation:
  // Feel alone (chrome hidden) until the next physical scene is associated.
  menu.SetChoiceTailFeelingVisible(true);
  menu.SetMeterOnly(true);
  paint();
  const auto soloMeter = sample(200, 5);
  const auto soloChrome = sample(0, 40);
  const auto soloAuto = sample(50, 10);
  Check(soloMeter[0] == 5 && soloMeter[1] == 235 &&
            soloMeter[2] == 245 &&
            soloChrome[0] == 0 && soloChrome[1] == 0 &&
            soloChrome[2] == 0 &&
            soloAuto[0] == 5 && soloAuto[1] == 235 &&
            soloAuto[2] == 245,
        "point change did not show full-alpha Feel with standalone AUTO ON");

  // The confirmed choice stays in its frozen result phase across scene bind:
  // the normal top controls must not come back merely because the script
  // advanced. The meter and AUTO ON retain their own independent surfaces.
  menu.SetMeterOnly(false);
  paint();
  const auto frozenMeter = sample(200, 5);
  const auto frozenChrome = sample(0, 40);
  const auto frozenAuto = sample(50, 10);
  Check(frozenMeter[0] == 5 && frozenMeter[1] == 235 &&
            frozenMeter[2] == 245 &&
            frozenChrome[0] == 0 && frozenChrome[1] == 0 &&
            frozenChrome[2] == 0 &&
            frozenAuto[0] == 5 && frozenAuto[1] == 235 &&
            frozenAuto[2] == 245,
        "scene rebind revealed chrome or obscured Feel/AUTO while frozen");

  // An actual pointer move away *during* the frozen result must invalidate
  // earlier top-hover probes. A stale old hover must never pop the chrome at
  // release when the cursor is currently outside the top strip.
  SDL_Event movedAway = hover;
  movedAway.motion.y = viewport.y + 250.0f * viewport.scale;
  (void)menu.HandleEvent(&renderer, &movedAway, &action);

  // After the frozen tail ends, no synthetic mouse hover should be inherited
  // from the old choice. A real new move into the top region alone may reveal
  // chrome. Outside the region, only AUTO ON remains on top of gameplay.
  menu.SetChoiceTailFrozen(false);
  menu.SetChoiceTailFeelingVisible(false);
  menu.Update(nowMs + 5000u, 10, 20);
  paint();
  const auto releasedChrome = sample(0, 40);
  const auto releasedAuto = sample(50, 10);
  Check(releasedChrome[0] == 0 && releasedChrome[1] == 0 &&
            releasedChrome[2] == 0 &&
            releasedAuto[0] == 5 && releasedAuto[1] == 235 &&
            releasedAuto[2] == 245,
        "MenuBar jumped visible at choice release instead of waiting for hover");

  // An omitted choice can request the host's Feeling refresh without any
  // actual raw 001/002 delta. A bare latch must not resurrect Feel after a
  // delay, especially after the post-choice freeze has been removed.
  menu.RequestFeelingRefresh();
  menu.Update(nowMs + 5500u, 10, 20); // Capture
  menu.Update(nowMs + 5501u, 10, 20); // Detect: both raw values unchanged
  Check(menu.ConsumeFeelingDirectionCode() == 0,
        "omitted/no-delta refresh invented SeUp or SeDown");
  paint();
  const auto omittedLateMeter = sample(200, 5);
  const auto omittedLateChrome = sample(0, 40);
  Check(omittedLateMeter[0] == 0 && omittedLateMeter[1] == 0 &&
            omittedLateMeter[2] == 0 &&
            omittedLateChrome[0] == 0 && omittedLateChrome[1] == 0 &&
            omittedLateChrome[2] == 0,
        "unchanged omitted choice resurrected standalone Feel after unlock");
  menu.Update(nowMs + 8500u, 10, 20);
  paint();
  const auto omittedAfterHold = sample(200, 5);
  Check(omittedAfterHold[0] == 0 && omittedAfterHold[1] == 0 &&
            omittedAfterHold[2] == 0,
        "omitted choice displayed Feel again after the hold timer");
  // The no-delta PostTarget path passes through Finalize on the next tick.
  // Finish that cycle before simulating a distinct omitted-choice result.
  menu.Update(nowMs + 8501u, 10, 20);

  // RouteProc can apply a nonzero raw 001/002 delta even when SETSELECT's
  // result is -1. That must not become a standalone Feel after the runtime
  // exits the frozen omission phase, regardless of the detector's sound cue.
  menu.SetChoiceTailFrozen(true);
  menu.SetChoiceTailFeelingVisible(false);
  menu.RequestFeelingRefresh();
  menu.Update(nowMs + 9000u, 11, 20); // Capture actual nonzero omission delta
  menu.Update(nowMs + 9001u, 11, 20); // Detect
  Check(menu.ConsumeFeelingDirectionCode() == 3,
        "synthetic omission delta failed to exercise real Feeling detector");
  menu.SetChoiceTailFrozen(false);
  paint();
  const auto omittedChangedMeter = sample(200, 5);
  Check(omittedChangedMeter[0] == 0 && omittedChangedMeter[1] == 0 &&
            omittedChangedMeter[2] == 0,
        "omitted choice with raw feeling delta revealed late standalone Feel");

  (void)menu.HandleEvent(&renderer, &hover, &action);
  menu.Update(static_cast<uint32_t>(SDL_GetTicks()) + 350u, 10, 20);
  paint();
  const auto hoveredChrome = sample(0, 40);
  Check(hoveredChrome[0] == 220 && hoveredChrome[1] == 10 &&
            hoveredChrome[2] == 10,
        "Top Menu did not respond to a fresh real hover after choice release");

  // Real retail A03 recording: after manual selection, the cursor enters
  // the meter's top strip during the result animation and stays there. Chrome
  // remains hidden until the result gate releases; then it may return without
  // requiring another mouse motion. The event MUST NOT activate a button or
  // paint chrome while frozen, and must not resurrect Feel on omission.
  menu.SetChoiceTailFrozen(true);
  (void)menu.HandleEvent(&renderer, &hover, &action);
  Check(action.kind == Presenter::ActionKind::None,
        "mouse entered a frozen result and activated MenuBar control");
  paint();
  const auto actuallyFrozenChrome = sample(0, 40);
  Check(actuallyFrozenChrome[0] == 0 && actuallyFrozenChrome[1] == 0 &&
            actuallyFrozenChrome[2] == 0,
        "hover while result was frozen exposed chrome prematurely");
  menu.SetChoiceTailFrozen(false);
  menu.Update(static_cast<uint32_t>(SDL_GetTicks()) + 350u, 10, 20);
  paint();
  const auto releasedUnderPointer = sample(0, 40);
  Check(releasedUnderPointer[0] == 220 && releasedUnderPointer[1] == 10 &&
            releasedUnderPointer[2] == 10,
        "retail A03: frozen physical hover did not reveal chrome on unlock");
  menu.Close();
}

} // namespace

namespace SchoolDays {

class RuntimeMenuBarTestAccess {
public:
  using Presenter = Kotonoha::SchoolDaysMenuBarPresenter;

  static void Apply(Runtime &runtime, Presenter::ActionKind kind,
                    int value = -1) {
    Presenter::Action action;
    action.kind = kind;
    action.value = value;
    runtime.activateMenuAction(action);
  }

  static double Speed(const Runtime &runtime) { return runtime.speed; }
  static int SpeedIndex(const Runtime &runtime) { return runtime.speedIndex; }
  static bool StoryAudioSuppressed(Runtime &runtime) {
    if (!runtime.engine) return false;
    for (const char *channel : {"BGM", "Voice", "SE", "Movie"})
      if (!runtime.engine->GetAudio().channel(channel)->isMuted()) return false;
    return true;
  }
  static bool StoryAudioEnabled(Runtime &runtime) {
    if (!runtime.engine) return false;
    for (const char *channel : {"BGM", "Voice", "SE", "Movie"})
      if (runtime.engine->GetAudio().channel(channel)->isMuted()) return false;
    return true;
  }
  static bool Paused(const Runtime &runtime) { return runtime.paused; }
  static bool ConfigDirty(const Runtime &runtime) {
    return runtime.configStorage.IsDirty();
  }
  static bool SpeedControlsEnabled(const Runtime &runtime) {
    return runtime.menuBarProjection().speedControlsEnabled;
  }
  static bool TransportLocked(const Runtime &runtime) {
    return runtime.menuBarProjection().transportLocked;
  }
  static bool ActionInhibit(const Runtime &runtime) {
    return runtime.menuBarProjection().actionInhibit;
  }
  static bool FullBarSuppressed(const Runtime &runtime) {
    return runtime.menuBarProjection().fullBarSuppressed;
  }
  static bool ReplayIndicator(const Runtime &runtime) {
    return runtime.menuBarProjection().replayIndicator;
  }
  static void StartOrdinaryReplay(Runtime &runtime) {
    runtime.startReplayPlayback(1);
  }
  static void TickPanel(Runtime &runtime, uint64_t nowMs) {
    runtime.updateSystemPanelTransition(nowMs);
  }
  static bool ClosePanel(Runtime &runtime) {
    return runtime.beginSystemPanelClose();
  }
};

} // namespace SchoolDays

int main(int argc, char **argv) {
  try {
    Check(argc == 3,
          "usage: SchoolDaysMenuBarRuntimeTest <route> <fixture-root>");
    TemporaryDirectory temporary;
    const auto documents = temporary.path / "documents";
    fs::create_directories(documents);
    auto assets = std::make_shared<SchoolDays::Assets>(argv[2]);

    using Access = SchoolDays::RuntimeMenuBarTestAccess;
    using Kind = Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
    {
      Application app(assets, argv[1], documents);
      Access::Apply(app.runtime, Kind::OpenLoad);
      const uint64_t openedAt = SDL_GetTicks();
      Check(!app.runtime.anyPanelOpen(),
            "gameplay LOAD opened before its white transition");
      Access::TickPanel(app.runtime, openedAt + 600u);
      Check(!app.runtime.anyPanelOpen(),
            "gameplay LOAD opened before the 750 ms boundary");
      Access::TickPanel(app.runtime, openedAt + 800u);
      Check(app.runtime.anyPanelOpen(),
            "gameplay LOAD did not open at the white boundary");
      Check(Access::ClosePanel(app.runtime), "gameplay LOAD close did not arm fade");
      const uint64_t closedAt = SDL_GetTicks();
      Access::TickPanel(app.runtime, closedAt + 400u);
      Check(app.runtime.anyPanelOpen(),
            "gameplay LOAD released its owner before close fade completed");
      Access::TickPanel(app.runtime, closedAt + 600u);
      Check(!app.runtime.anyPanelOpen(),
            "gameplay LOAD remained open after close fade");
      Access::TickPanel(app.runtime, closedAt + 1200u);
    }
    {
      Application app(assets, argv[1], documents);
      Check(!app.runtime.retailConfig().autoMode,
            "default AutoMode is unexpectedly enabled");

      CheckMeterSfxHostCodes(app, temporary.path);
      CheckMeterCompositingPixels(app, temporary.path);

      Access::Apply(app.runtime, Kind::ToggleAutoMode);
      Check(app.runtime.retailConfig().autoMode,
            "MenuBar Auto did not update retail config state");
      Check(Access::ConfigDirty(app.runtime),
            "MenuBar Auto did not cache Config.DAT state");

      Access::Apply(app.runtime, Kind::TogglePause);
      Check(Access::Paused(app.runtime), "MenuBar Pause did not pause runtime");
      Access::Apply(app.runtime, Kind::TogglePause);
      Check(!Access::Paused(app.runtime), "second MenuBar Pause did not resume runtime");

      constexpr double expected[] = {1.0, 2.0, 4.0, 12.0, 24.0};
      for (int index = 0; index < 5; ++index) {
        Access::Apply(app.runtime, Kind::SetPlaybackSpeed, index);
        Check(Access::SpeedIndex(app.runtime) == index &&
                  Near(Access::Speed(app.runtime), expected[index]),
              "MenuBar playback-speed mapping drifted");
        Check(index <= 2 ? Access::StoryAudioEnabled(app.runtime)
                         : Access::StoryAudioSuppressed(app.runtime),
              "Narrative audio must mute above internal rate 4 while maintaining media clock");
      }

      const double before = Access::Speed(app.runtime);
      Access::Apply(app.runtime, Kind::SetReplayIndicatorOpacity, 10);
      Check(Near(Access::Speed(app.runtime), before),
            "ordinary Replay-opacity action contaminated runtime speed");

      bool rejected = false;
      try {
        Access::Apply(app.runtime, Kind::SetPlaybackSpeed, 5);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      Check(rejected, "invalid MenuBar speed index did not fail closed");

      Check(Access::SpeedControlsEnabled(app.runtime) &&
                !Access::TransportLocked(app.runtime) &&
                !Access::ActionInhibit(app.runtime) &&
                !Access::FullBarSuppressed(app.runtime) &&
                !Access::ReplayIndicator(app.runtime),
            "normal Gameplay MenuBar projection drifted");

      Access::StartOrdinaryReplay(app.runtime);
      Check(Access::SpeedControlsEnabled(app.runtime) &&
                !Access::TransportLocked(app.runtime) &&
                Access::ActionInhibit(app.runtime) &&
                !Access::FullBarSuppressed(app.runtime) &&
                !Access::ReplayIndicator(app.runtime),
            "ordinary HScene Replay MenuBar action-inhibit projection drifted");
      Access::Apply(app.runtime, Kind::OpenSave);
      Check(!app.runtime.panels().IsSaveLoadOpen(),
            "ordinary HScene Replay exposed nested SAVE");
    }

    Kotonoha::SchoolDaysConfigStorage storage;
    std::string error;
    Kotonoha::SchoolDaysConfigStorage::LoadResult loaded;
    Check(storage.Initialize(documents, &error), error);
    Check(storage.Load(&loaded, &error), error);
    Check(loaded.state.autoMode,
          "MenuBar Auto cache was not flushed to Config.DAT on shutdown");

    std::cout << "SchoolDaysMenuBarRuntimeTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysMenuBarRuntimeTest: " << error.what() << '\n';
    return 1;
  }
}
