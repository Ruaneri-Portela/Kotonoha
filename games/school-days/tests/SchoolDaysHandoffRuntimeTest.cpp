#include "SchoolDaysAssets.hpp"
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/SchoolDaysAssetMount.hpp>
#include <Kotonoha/routing/SchoolDaysGpkLanguageProfile.hpp>
#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {

void Require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

void Put(const fs::path &path, const std::string &data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  Require(bool(out), "Cannot write " + path.string());
}

void PutBytes(const fs::path &path, const std::vector<uint8_t> &bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  Require(bool(out), "Cannot write " + path.string());
}

void WriteCmap(const fs::path &path) {
  std::vector<uint8_t> bytes(9u, 0u);
  bytes[0] = 1u;
  bytes[4] = 1u;
  bytes[8] = 1u;
  PutBytes(path, bytes);
}

void WritePng(const fs::path &path) {
  static constexpr std::array<uint8_t, 70> png{{
      0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,
      0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,
      0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x78,
      0x9c,0x63,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x05,0x00,0x01,0xa5,0xf6,
      0x45,0x40,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82}};
  PutBytes(path, std::vector<uint8_t>(png.begin(), png.end()));
}

void WriteSystemUiAssets(const fs::path &root) {
  const auto meter = root / "System" / "MenuBar";
  WritePng(meter / "MenuBar.PNG");
  WritePng(meter / "MenuBar_Chip.PNG");
  const auto saveLoad = root / "System" / "SaveLoad";
  WriteCmap(saveLoad / "Pop_Save_Feel.cmap");
  WriteCmap(saveLoad / "Pop_Save_Feel_Wide.cmap");
  WriteCmap(saveLoad / "Pop_Save_Feel_Wide_Full.cmap");
  WriteCmap(saveLoad / "Pop_Save_Feel_Wide_Note.cmap");
  WriteCmap(saveLoad / "SaveLoad.cmap");
  WriteCmap(saveLoad / "SaveLoad_Wide.cmap");
  WriteCmap(saveLoad / "SaveLoad_Wide_Full.cmap");
  WriteCmap(saveLoad / "SaveLoad_Wide_Note.cmap");
  WritePng(saveLoad / "Pop_Save_Feel.PNG");
  WritePng(saveLoad / "Pop_Save_Feel_Chip.PNG");
  WritePng(saveLoad / "Save.PNG");
  WritePng(saveLoad / "Load.PNG");
  WritePng(saveLoad / "SaveLoad_Chip.PNG");

  const auto exit = root / "System" / "Exit";
  WriteCmap(exit / "Popup.cmap");
  WriteCmap(exit / "Popup_Wide.cmap");
  WriteCmap(exit / "Popup_Wide_Full.cmap");
  WriteCmap(exit / "Popup_Wide_Note.cmap");
  // MainMenu confirmation must use Popup_Title. Deliberately do not provide
  // Popup_Exit here so this fixture fails if Runtime selects the wrong base.
  WritePng(exit / "Popup_Title.PNG");
  WritePng(exit / "Popup_Chip.PNG");

  PutBytes(root / "System" / "System" / "FONTDATA.DAT",
           std::vector<uint8_t>(65536u * 4u, 0u));
  Put(root / "ini" / "FILMENGINE.INI",
      "[UseEnglish]=\"0\"\n"
      "[TextInput]=\"1\"\n"
      "[FontData]=\"System/System/FONTDATA.DAT\"\n"
      "[SaveFolder]=\"Save\"\n"
      "[SaveFileName]=\"Save/SaveFile00%d.DAT\"\n"
      "[FlagFileName]=\"Save/GlobalFlag.DAT\"\n"
      "[SaveConfig]=\"FILMEngine/SaveFile00%d\"\n"
      "[SeCancel]=\"missing/cancel\"\n"
      "[SeSelect]=\"missing/select\"\n"
      "[SeClick]=\"missing/click\"\n"
      "[SeUp]=\"missing/up\"\n"
      "[SeDown]=\"missing/down\"\n"
      "[SeView]=\"missing/view\"\n"
      "[SeOpen]=\"missing/open\"\n");
}

struct TemporaryDirectory {
  fs::path path = fs::current_path() /
                  ("school-days-handoff-" +
                   std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    Require(fs::create_directory(path), "Cannot create temporary directory");
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};

Kotonoha::EngineOptions Options(const std::shared_ptr<SchoolDays::Assets> &assets) {
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
              const char *route, const fs::path &documents,
              const fs::path &lxhJson = {}, const std::string &nativeLocale = {})
      : runtime(assets, route, documents), engine(runtime, Options(assets)) {
    if (!lxhJson.empty()) {
      Kotonoha::Translation translation;
      translation.load(lxhJson.string());
      runtime.setLxhTranslation(std::move(translation), "en");
    }
    if (!nativeLocale.empty()) runtime.setLxhTranslation({}, nativeLocale, true);
    runtime.SetState(Kotonoha::GameState::Gameplay);
  }
  ~Application() { runtime.shutdown(); }
};

SDL_Event EscapeEvent() {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = SDLK_ESCAPE;
  event.key.repeat = false;
  return event;
}

SDL_Event MotionEvent() {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.x = 640.0f;
  event.motion.y = 360.0f;
  return event;
}

SDL_Event ClickEvent() {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = 640.0f;
  event.button.y = 360.0f;
  return event;
}

} // namespace

namespace SchoolDays {

class RuntimeHandoffTestAccess {
public:
  static void Advance(Runtime &runtime) { runtime.advance(); }

  static void AdvanceDeterministic(Runtime &runtime) {
    const auto *adapter = runtime.bridge.Session().Adapter();
    uint32_t count = 0;
    Kotonoha_KtrfError error{};
    Require(Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &count, &error),
            error.message);
    if (count != 0u) {
      uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
      Require(count == 1u &&
                  Kotonoha_KtrfRouterCurrentChoiceAt(&adapter->router, 0u,
                                                     &choiceIndex, &error),
              "deterministic terminal path choice lookup failed");
      Kotonoha_KtrfChoice choice{};
      Kotonoha_KtrfChoiceOption option{};
      Require(Kotonoha_KtrfGetChoice(adapter->document, choiceIndex, &choice, &error) &&
                  choice.option_count > 0u &&
                  Kotonoha_KtrfGetChoiceOption(adapter->document, choiceIndex, 0u,
                                               &option, &error) &&
                  option.value.kind == KOTONOHA_KTRF_SCALAR_INT64,
              "deterministic terminal path first choice is unavailable");
      int accepted = 0;
      Require(runtime.bridge.CommitChoice(option.value.as.int64_value, &accepted, &error) &&
                  accepted != 0,
              "deterministic terminal path choice commit failed");
    }
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
    Require(runtime.bridge.Advance("ktrf:next", &result, &error), error.message);
    using Kind = Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind;
    if (result.kind == Kind::SwappedScene) {
      runtime.openScene(result.scene);
    } else if (result.kind == Kind::PendingHandoff || result.kind == Kind::Terminal) {
      runtime.armHandoff();
    } else {
      Require(false, "deterministic terminal path returned unexpected bridge result");
    }
  }

  static bool Deferred(const Runtime &runtime) {
    return runtime.handoffUiState == Runtime::HandoffUiState::Deferred;
  }

  static bool SaveFeel(const Runtime &runtime) {
    return runtime.handoffUiState == Runtime::HandoffUiState::SaveFeel;
  }

  static bool SaveLoad(const Runtime &runtime) {
    return runtime.handoffUiState == Runtime::HandoffUiState::SaveLoad;
  }

  static bool Idle(const Runtime &runtime) {
    return runtime.handoffUiState == Runtime::HandoffUiState::Idle;
  }

  static bool Pending(const Runtime &runtime) { return runtime.bridge.HasPendingHandoff(); }
  static bool PendingTerminal(const Runtime &runtime) {
    return runtime.bridge.PendingHandoffIsTerminal();
  }
  static Kotonoha::SchoolDaysDisplayPolicy::Variant UiVariant(
      const Runtime &runtime) {
    return runtime.uiVariant;
  }
  static bool HasGameplay(const Runtime &runtime) {
    return runtime.bridge.CurrentGameplay() != nullptr;
  }

  static bool ExitFailureReported(const Runtime &runtime) {
    return runtime.status.rfind("Exit confirmation unavailable:", 0u) == 0u;
  }

  static void SetFeelings(Runtime &runtime, int64_t one, int64_t two) {
    Kotonoha_KtrfError error{};
    Require(runtime.bridge.Session().State().WriteSessionInt("001", one, &error),
            error.message);
    Require(runtime.bridge.Session().State().WriteSessionInt("002", two, &error),
            error.message);
  }

  static void OpenMeter(Runtime &runtime) {
    Require(runtime.gameScene && runtime.gameScene->ensureMenuBar(),
            "cannot open the real MenuBar before changing SaveFile");
  }

  static void RebaseMeter(Runtime &runtime) {
    runtime.gameScene->restoreMenuBarFeelingState();
  }

  static Kotonoha::SchoolDaysTopMeterModel::Visual MeterVisual(Runtime &runtime) {
    Require(runtime.gameScene && runtime.gameScene->menuBar.IsOpen(),
            "MenuBar was unexpectedly replaced or closed");
    return runtime.gameScene->menuBar.Meter().CurrentVisual();
  }

  static int64_t Feeling(Runtime &runtime, const char *key) {
    int64_t value = 0;
    Kotonoha_KtrfError error{};
    Require(runtime.bridge.Session().State().ReadSessionInt(key, &value, &error),
            error.message);
    return value;
  }

  static void LoadSlot(Runtime &runtime, int slot) { runtime.loadSlot(slot); }
  static void SaveSlot(Runtime &runtime, int slot) { runtime.saveSlot(slot, u""); }
  static bool LxhFadeEnabled(const Runtime &runtime) { return runtime.lxhFade.Enabled(); }
  static uint32_t NextTick(const Runtime &runtime) {
    const auto next = runtime.timeline.NextTick();
    return next ? *next : 0u;
  }
  static void StartFresh(Runtime &runtime) { runtime.reset(); }

  static bool MeterLatch(Runtime &runtime) {
    return runtime.gameScene->menuBar.FeelingLatch();
  }

  static void OpenDeferred(Runtime &runtime) { runtime.updateHandoffUi(); }

  static void SubmitSaveFeel(Runtime &runtime,
                             Kotonoha::SchoolDaysSaveFeelModel::Result result) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    output.hasSaveFeelResult = true;
    output.saveFeelResult = result;
    runtime.handlePanel(output);
  }

  static void TickSaveFeel(Runtime &runtime, uint32_t now) {
    int64_t one = 0;
    int64_t two = 0;
    Kotonoha_KtrfError error{};
    Require(runtime.bridge.Session().State().ReadSessionInt("001", &one, &error),
            error.message);
    Require(runtime.bridge.Session().State().ReadSessionInt("002", &two, &error),
            error.message);
    Kotonoha::SchoolDaysSystemUi::Output output;
    runtime.systemUi.Update(now, one, two, &output);
    runtime.handlePanel(output);
  }

  static void SubmitSaveLoad(Runtime &runtime,
                             Kotonoha::SchoolDaysSaveLoadModel::Action action,
                             int slot = -1) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    output.hasSaveLoadOutput = true;
    output.saveLoadOutput.action = action;
    output.saveLoadOutput.slot = slot;
    runtime.handlePanel(output);
  }

  static void ContinueAgain(Runtime &runtime) { runtime.continueHandoff(); }

  static void ApplyMenu(Runtime &runtime,
                        Kotonoha::SchoolDaysMenuBarPresenter::ActionKind kind) {
    Kotonoha::SchoolDaysMenuBarPresenter::Action action;
    action.kind = kind;
    runtime.activateMenuAction(action);
  }

  static void SubmitExit(Runtime &runtime,
                         Kotonoha::SchoolDaysExitViewModel::Outcome outcome) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    output.hasExitActivation = true;
    output.exitActivation.outcome = outcome;
    runtime.handlePanel(output);
  }

  static void TickExit(Runtime &runtime, uint64_t now) {
    runtime.updateExitTransition(now);
  }

  static Kotonoha::Gameplay *GameplayOwner(const Runtime &runtime) {
    return runtime.bridge.CurrentGameplay();
  }

  static void StartReplay(Runtime &runtime, int index) {
    runtime.startReplayPlayback(index);
  }

  static bool Temporary(const Runtime &runtime) {
    return runtime.temporaryPlaybackActive();
  }

  static void PrepareTitle(Runtime &runtime) {
    runtime.frontendTransition.Reset();
    runtime.frontendHandoff = Runtime::FrontendHandoff::None;
    runtime.frontendChild = Runtime::FrontendChild::None;
    runtime.openTitle(false);
  }

  static void OpenFirstVisibleTitle(Runtime &runtime) {
    runtime.frontendTransition.Reset();
    runtime.frontendHandoff = Runtime::FrontendHandoff::None;
    runtime.frontendChild = Runtime::FrontendChild::None;
    runtime.titleHasPresented = false;
    runtime.openTitle(true);
  }

  static void BeginTitleLoad(Runtime &runtime, uint64_t now) {
    runtime.beginTitleHandoff(Runtime::FrontendHandoff::TitleLoad, now);
  }

  static void BeginTitleStart(Runtime &runtime, uint64_t now) {
    runtime.beginTitleHandoff(Runtime::FrontendHandoff::TitleStart, now);
  }

  static void BeginChildReturn(Runtime &runtime, uint64_t now) {
    runtime.beginChildToTitle(now);
  }

  static void TickFrontend(Runtime &runtime, uint64_t now) {
    runtime.updateFrontendTransition(now);
  }

  static Kotonoha::SchoolDaysFrontendTransition::Phase FrontendPhase(
      Runtime &runtime, uint64_t now) {
    return runtime.frontendTransition.CurrentPhase(now);
  }

  static Kotonoha::SchoolDaysFrontendTransition::Color FrontendColor(
      const Runtime &runtime) {
    return runtime.frontendTransition.CurrentColor();
  }

  static bool FrontendChildLoad(const Runtime &runtime) {
    return runtime.frontendChild == Runtime::FrontendChild::Load;
  }

  static bool FrontendHandoffIdle(const Runtime &runtime) {
    return runtime.frontendHandoff == Runtime::FrontendHandoff::None;
  }
};

} // namespace SchoolDays

namespace {

void PrepareAssets(const fs::path &sourceFixture, const fs::path &root) {
  fs::copy(sourceFixture / "Script", root / "Script",
           fs::copy_options::recursive | fs::copy_options::overwrite_existing);
  WriteSystemUiAssets(root);
}

void PrepareInitialNarrative(SchoolDays::Runtime &runtime) {
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  // Public runtimes start on A00 and have no developer jump implementation.
  Require(runtime.debugNode() == "00/00-00-A00",
          "DIST narrative fixture did not start at its route entry");
#else
  runtime.debugJumpToNode("00/00-00-A00");
#endif
}

void ArmFirstEpisodeHandoff(SchoolDays::Runtime &runtime) {
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  // Reach the first real episode boundary using the gameplay bridge, which is
  // also how a player reaches it. The development jump is absent in DIST.
  for (int step = 0;
       step < 256 && !SchoolDays::RuntimeHandoffTestAccess::Pending(runtime);
       ++step) {
    SchoolDays::RuntimeHandoffTestAccess::AdvanceDeterministic(runtime);
  }
  Require(runtime.debugNode() == "00/00-00-L00",
          "DIST first episode path did not reach the L00 boundary");
#else
  runtime.debugJumpToNode("00/00-00-L00");
  SchoolDays::RuntimeHandoffTestAccess::Advance(runtime);
#endif
  Require(SchoolDays::RuntimeHandoffTestAccess::Pending(runtime),
          "L00 did not produce a pending episode handoff");
  Require(!SchoolDays::RuntimeHandoffTestAccess::PendingTerminal(runtime),
          "L00 unexpectedly produced terminal handoff");
  Require(SchoolDays::RuntimeHandoffTestAccess::Deferred(runtime),
          "pending handoff did not enter one-update Deferred state");
  Require(!runtime.panels().IsSaveFeelOpen(),
          "SaveFeel opened synchronously instead of deferring one update");
}

void TestSaveFeelAndSaveLoad(const char *route, const fs::path &root) {
  const auto documents = root / "documents-normal";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  SchoolDays::RuntimeHandoffTestAccess::SetFeelings(app.runtime, 73, 21);

  ArmFirstEpisodeHandoff(app.runtime);
  app.runtime.update();
  Require(SchoolDays::RuntimeHandoffTestAccess::SaveFeel(app.runtime) &&
              app.runtime.panels().IsSaveFeelOpen(),
          "next Runtime update did not open deferred SaveFeel");
  Require(app.runtime.panels().SaveFeel().CurrentVariant() ==
              SchoolDays::RuntimeHandoffTestAccess::UiVariant(app.runtime),
          "handoff SaveFeel did not use the Runtime display projection");

  auto escape = EscapeEvent();
  app.runtime.onEvent(escape);
  Require(app.runtime.panels().IsSaveFeelOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime),
          "Escape closed/stranded handoff SaveFeel");

  SchoolDays::RuntimeHandoffTestAccess::SubmitSaveFeel(
      app.runtime, Kotonoha::SchoolDaysSaveFeelModel::Result::OpenSave);
  Require(SchoolDays::RuntimeHandoffTestAccess::SaveLoad(app.runtime) &&
              app.runtime.panels().IsSaveLoadOpen() &&
              app.runtime.panels().SaveLoad().CurrentMode() ==
                  Kotonoha::SchoolDaysSaveLoadModel::Mode::Save &&
              SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime),
          "OpenSave did not enter retail SaveLoad SAVE while bridge stayed pending");

  auto motion = MotionEvent();
  auto click = ClickEvent();
  app.runtime.onEvent(motion);
  app.runtime.onEvent(click);
  Require(app.runtime.panels().IsSaveLoadCommentEditorOpen(),
          "handoff SaveLoad row did not open comment editor");
  app.runtime.onEvent(escape);
  Require(!app.runtime.panels().IsSaveLoadCommentEditorOpen() &&
              app.runtime.panels().IsSaveLoadOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime),
          "comment Escape closed/stranded handoff SaveLoad");

  app.runtime.onEvent(escape);
  Require(app.runtime.panels().IsSaveLoadOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime),
          "outer Escape closed/stranded handoff SaveLoad");

  SchoolDays::RuntimeHandoffTestAccess::SubmitSaveLoad(
      app.runtime, Kotonoha::SchoolDaysSaveLoadModel::Action::SaveSlotRequested, 0);
  Require(app.runtime.panels().IsSaveLoadOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::SaveLoad(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime),
          "successful handoff save left SaveLoad or continued bridge");
  Require(fs::is_regular_file(documents / "Save" / "SaveFile000.DAT"),
          "handoff SaveLoad did not write slot 0 successfully");

  SchoolDays::RuntimeHandoffTestAccess::SubmitSaveLoad(
      app.runtime, Kotonoha::SchoolDaysSaveLoadModel::Action::Close);
  Require(!SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::Idle(app.runtime) &&
              !app.runtime.anyPanelOpen() &&
              app.runtime.debugNode() == "01/01-00-A00",
          "explicit handoff SaveLoad Close did not continue exactly to Episode 2");
  SchoolDays::RuntimeHandoffTestAccess::ContinueAgain(app.runtime);
  Require(app.runtime.debugNode() == "01/01-00-A00",
          "duplicate ContinueHandoff changed scene after completion");

  // Regression for loading a different ending while the SAME game process
  // and physical MenuBar stay alive. Before the fix, Open() was skipped and
  // the previous saturated green fill persisted indefinitely until exit.
  using Access = SchoolDays::RuntimeHandoffTestAccess;
  Access::SetFeelings(app.runtime, -200, 200);
  Access::OpenMeter(app.runtime);
  // The presenter may already be open from earlier real UI frames; establish
  // the A-save display explicitly before the subsequent genuine B-slot LOAD.
  Access::RebaseMeter(app.runtime);
  Require(Access::MeterVisual(app.runtime).primary == -1000.0f,
          "saved ending with extreme feelings did not saturate the meter");
  Access::LoadSlot(app.runtime, 0);
  const int64_t restored001 = Access::Feeling(app.runtime, "001");
  const int64_t restored002 = Access::Feeling(app.runtime, "002");
  const auto restored = Access::MeterVisual(app.runtime);
  Require(restored.primary == static_cast<float>(restored001 - restored002) * 2.5f &&
              restored.opposite == -restored.primary &&
              restored.primary != -1000.0f && !Access::MeterLatch(app.runtime),
          "LOAD in the same process kept the saturated meter from the previous save");
  Access::SetFeelings(app.runtime, 200, -200);
  // Normally a session refresh has a 1500ms animation. A NEW GAME is a
  // session replacement, so it must use the new initial feelings immediately.
  Access::StartFresh(app.runtime);
  const int64_t fresh001 = Access::Feeling(app.runtime, "001");
  const int64_t fresh002 = Access::Feeling(app.runtime, "002");
  const auto fresh = Access::MeterVisual(app.runtime);
  Require(fresh.primary == static_cast<float>(fresh001 - fresh002) * 2.5f &&
              fresh.opposite == -fresh.primary && !Access::MeterLatch(app.runtime),
          "new gameplay after an ending inherited its old meter state");
}

void TestOrdinaryCommentEscape(const char *route, const fs::path &root) {
  const auto documents = root / "documents-comment-ordinary";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  PrepareInitialNarrative(app.runtime);
  app.runtime.openPanel(10);
  Require(app.runtime.panels().IsSaveLoadOpen(),
          "ordinary SaveLoad did not open");
  auto motion = MotionEvent();
  auto click = ClickEvent();
  app.runtime.onEvent(motion);
  app.runtime.onEvent(click);
  Require(app.runtime.panels().IsSaveLoadCommentEditorOpen(),
          "ordinary SaveLoad row did not open comment editor");
  auto escape = EscapeEvent();
  app.runtime.onEvent(escape);
  Require(!app.runtime.panels().IsSaveLoadCommentEditorOpen() &&
              app.runtime.panels().IsSaveLoadOpen() &&
              !SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime),
          "comment Escape closed ordinary SaveLoad or changed bridge state");
}

void TestMainMenuConfirmNarrative(const char *route, const fs::path &root) {
  const auto documents = root / "documents-mainmenu-narrative";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  PrepareInitialNarrative(app.runtime);
  auto *owner = SchoolDays::RuntimeHandoffTestAccess::GameplayOwner(app.runtime);
  const std::string node = app.runtime.debugNode();
  Require(owner != nullptr, "narrative MainMenu fixture has no Gameplay owner");

  using MenuKind = Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
  using Outcome = Kotonoha::SchoolDaysExitViewModel::Outcome;
  SchoolDays::RuntimeHandoffTestAccess::ApplyMenu(
      app.runtime, MenuKind::OpenMainMenuConfirm);
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  Require(app.runtime.panels().IsExitOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::GameplayOwner(app.runtime) == owner &&
              app.runtime.debugNode() == node &&
              app.runtime.GetState() == Kotonoha::GameState::Gameplay,
          "MainMenu confirmation destroyed narrative owner before a result");
  SchoolDays::RuntimeHandoffTestAccess::SubmitExit(
      app.runtime, Outcome::ReturnToTitle);
  Require(!app.runtime.anyPanelOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::GameplayOwner(app.runtime) == owner &&
              app.runtime.debugNode() == node &&
              app.runtime.GetState() == Kotonoha::GameState::Gameplay,
          "MainMenu NO did not resume the same narrative owner");
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);

  SchoolDays::RuntimeHandoffTestAccess::ApplyMenu(
      app.runtime, MenuKind::OpenMainMenuConfirm);
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  SchoolDays::RuntimeHandoffTestAccess::SubmitExit(
      app.runtime, Outcome::ConfirmExit);
  Require(SchoolDays::RuntimeHandoffTestAccess::GameplayOwner(app.runtime) == owner,
          "MainMenu YES released narrative Gameplay before fade completed");
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  Require(SchoolDays::RuntimeHandoffTestAccess::GameplayOwner(app.runtime) == nullptr &&
              app.runtime.GetState() == Kotonoha::GameState::Menu,
          "MainMenu YES did not release narrative Gameplay and return to Title");
}

void TestMainMenuConfirmTemporary(const char *route, const fs::path &root) {
  const auto documents = root / "documents-mainmenu-replay";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  PrepareInitialNarrative(app.runtime);
  auto *liveOwner = SchoolDays::RuntimeHandoffTestAccess::GameplayOwner(app.runtime);
  SchoolDays::RuntimeHandoffTestAccess::StartReplay(app.runtime, 1);
  Require(SchoolDays::RuntimeHandoffTestAccess::Temporary(app.runtime),
          "temporary Replay fixture did not start");
  const std::string replayNode = app.runtime.debugNode();

  using MenuKind = Kotonoha::SchoolDaysMenuBarPresenter::ActionKind;
  using Outcome = Kotonoha::SchoolDaysExitViewModel::Outcome;
  SchoolDays::RuntimeHandoffTestAccess::ApplyMenu(
      app.runtime, MenuKind::OpenMainMenuConfirm);
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  Require(app.runtime.panels().IsExitOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::Temporary(app.runtime) &&
              app.runtime.debugNode() == replayNode,
          "MainMenu confirmation ended temporary playback before a result");
  SchoolDays::RuntimeHandoffTestAccess::SubmitExit(
      app.runtime, Outcome::ReturnToTitle);
  Require(!app.runtime.anyPanelOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::Temporary(app.runtime) &&
              app.runtime.debugNode() == replayNode,
          "MainMenu NO did not resume the same temporary playback owner");
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);

  SchoolDays::RuntimeHandoffTestAccess::ApplyMenu(
      app.runtime, MenuKind::OpenMainMenuConfirm);
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  SchoolDays::RuntimeHandoffTestAccess::SubmitExit(
      app.runtime, Outcome::ConfirmExit);
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  Require(!SchoolDays::RuntimeHandoffTestAccess::Temporary(app.runtime) &&
              app.runtime.GetState() == Kotonoha::GameState::Menu &&
              SchoolDays::RuntimeHandoffTestAccess::GameplayOwner(app.runtime) == liveOwner,
          "MainMenu YES did not end only temporary playback and return to Title");
}

void TestMissingExitPopupDoesNotQuit(const char *route, const fs::path &root) {
  // Popup_Title exists in this fixture, but Popup_Exit is added only by the
  // later frontend test. A failed confirmation must not bypass the question.
  const auto documents = root / "documents-missing-exit-popup";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  PrepareInitialNarrative(app.runtime);
  SDL_FlushEvent(SDL_EVENT_QUIT);
  app.runtime.openPanel(16);
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  Require(!app.runtime.panels().IsExitOpen() && !SDL_HasEvent(SDL_EVENT_QUIT),
          "missing EXIT popup bypassed confirmation and queued process shutdown");
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
  Require(!SchoolDays::RuntimeHandoffTestAccess::ExitFailureReported(app.runtime),
          "DIST exposed an internal EXIT artwork diagnostic");
#else
  Require(SchoolDays::RuntimeHandoffTestAccess::ExitFailureReported(app.runtime),
          "DEV did not report missing EXIT confirmation artwork");
#endif
}

void TestFrontendTransitions(const char *route, const fs::path &root) {
  const auto documents = root / "documents-frontend-transition";
  fs::create_directories(documents);
  // Earlier MainMenu tests intentionally omit Popup_Exit to prove that the
  // confirmation uses Popup_Title. This scenario exercises ordinary Title Exit
  // itself, so add the missing base only after that protection has run.
  WritePng(root / "System" / "Exit" / "Popup_Exit.PNG");
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  using Transition = Kotonoha::SchoolDaysFrontendTransition;

  SchoolDays::RuntimeHandoffTestAccess::OpenFirstVisibleTitle(app.runtime);
  const uint64_t firstTitleNow = SDL_GetTicks();
  Require(SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                              firstTitleNow) ==
              Transition::Phase::Incoming &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendColor(app.runtime) ==
                  Transition::Color::Black,
          "first Title presentation did not start the authenticated black incoming fade");
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime,
                                                     firstTitleNow + 1000u);

  SchoolDays::RuntimeHandoffTestAccess::PrepareTitle(app.runtime);
  const uint64_t loadStart = SDL_GetTicks();
  SchoolDays::RuntimeHandoffTestAccess::BeginTitleLoad(app.runtime, loadStart);
  Require(app.runtime.GetState() == Kotonoha::GameState::Menu &&
              !app.runtime.panels().IsSaveLoadOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                                  loadStart) ==
                  Transition::Phase::Outgoing,
          "Title->Load changed ownership before the white outgoing fade");
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, loadStart + 499u);
  Require(!app.runtime.panels().IsSaveLoadOpen(),
          "Title->Load opened child before 500ms opacity boundary");
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, loadStart + 500u);
  Require(app.runtime.panels().IsSaveLoadOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendChildLoad(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendHandoffIdle(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                                  loadStart + 500u) ==
                  Transition::Phase::Incoming,
          "Title->Load did not hand off exactly at 500ms and begin incoming fade");

  // Runtime input uses the real SDL clock. The synthetic incoming phase starts
  // in the future relative to it, so clock-regression clamping keeps the fade
  // active and lets us prove Escape cannot bypass the child transition.
  auto escape = EscapeEvent();
  app.runtime.onEvent(escape);
  Require(app.runtime.panels().IsSaveLoadOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendHandoffIdle(app.runtime),
          "keyboard shortcut bypassed active frontend incoming fade");
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, loadStart + 1000u);
  Require(SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                              loadStart + 1000u) ==
              Transition::Phase::Idle,
          "Title->Load incoming fade did not finish at 500ms");

  const uint64_t backStart = loadStart + 1100u;
  SchoolDays::RuntimeHandoffTestAccess::BeginChildReturn(app.runtime, backStart);
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, backStart + 499u);
  Require(app.runtime.panels().IsSaveLoadOpen(),
          "Load->Title released child before outgoing fade reached opacity");
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, backStart + 500u);
  Require(!app.runtime.panels().IsSaveLoadOpen() &&
              app.runtime.GetState() == Kotonoha::GameState::Menu &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                                  backStart + 500u) ==
                  Transition::Phase::Incoming,
          "Load->Title did not swap to Title at the 500ms opacity boundary");
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, backStart + 1000u);
  Require(SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                              backStart + 1000u) ==
              Transition::Phase::Idle,
          "Load->Title incoming fade did not finish at 500ms");

  SchoolDays::RuntimeHandoffTestAccess::PrepareTitle(app.runtime);
  const uint64_t startAt = backStart + 2000u;
  SchoolDays::RuntimeHandoffTestAccess::BeginTitleStart(app.runtime, startAt);
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, startAt + 999u);
  Require(app.runtime.GetState() == Kotonoha::GameState::Menu,
          "Title->Start entered Gameplay before 1000ms black opacity boundary");
  SchoolDays::RuntimeHandoffTestAccess::TickFrontend(app.runtime, startAt + 1000u);
  Require(app.runtime.GetState() == Kotonoha::GameState::Gameplay &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                                  startAt + 1000u) ==
                  Transition::Phase::Idle,
          "Title->Start did not hand off at 1000ms with no incoming phase");

  SchoolDays::RuntimeHandoffTestAccess::PrepareTitle(app.runtime);
  app.runtime.openPanel(16);
  Require(!app.runtime.panels().IsExitOpen(),
          "Title Exit opened before the SYSBASE wipe completed");
  SchoolDays::RuntimeHandoffTestAccess::TickExit(app.runtime, SDL_GetTicks() + 500u);
  Require(app.runtime.panels().IsExitOpen() &&
              SchoolDays::RuntimeHandoffTestAccess::FrontendPhase(app.runtime,
                                                                  SDL_GetTicks()) ==
                  Transition::Phase::Idle,
          "Title Exit did not open after its game-owned SYSBASE wipe");
}

void TestTimeoutSkip(const char *route, const fs::path &root) {
  const auto documents = root / "documents-timeout";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  SchoolDays::RuntimeHandoffTestAccess::SetFeelings(app.runtime, 100, 50);
  ArmFirstEpisodeHandoff(app.runtime);
  SchoolDays::RuntimeHandoffTestAccess::OpenDeferred(app.runtime);
  Require(SchoolDays::RuntimeHandoffTestAccess::SaveFeel(app.runtime),
          "timeout fixture did not open SaveFeel");
  SchoolDays::RuntimeHandoffTestAccess::TickSaveFeel(app.runtime, 1000u);
  SchoolDays::RuntimeHandoffTestAccess::TickSaveFeel(app.runtime, 31000u);
  SchoolDays::RuntimeHandoffTestAccess::TickSaveFeel(app.runtime, 31000u);
  Require(!SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::Idle(app.runtime) &&
              app.runtime.debugNode() == "01/01-00-A00",
          "SaveFeel timeout did not take the authenticated Skip continuation once");
}

void TestExplicitSkip(const char *route, const fs::path &root) {
  const auto documents = root / "documents-explicit-skip";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);
  ArmFirstEpisodeHandoff(app.runtime);
  app.runtime.update();
  SchoolDays::RuntimeHandoffTestAccess::SubmitSaveFeel(
      app.runtime, Kotonoha::SchoolDaysSaveFeelModel::Result::Skip);
  Require(!SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::Idle(app.runtime) &&
              !app.runtime.anyPanelOpen() &&
              app.runtime.debugNode() == "01/01-00-A00",
          "explicit SaveFeel Skip did not continue the normal handoff once");
  SchoolDays::RuntimeHandoffTestAccess::ContinueAgain(app.runtime);
  Require(app.runtime.debugNode() == "01/01-00-A00",
          "explicit SaveFeel Skip allowed duplicate ContinueHandoff");
}

void TestTerminalRelease(const char *route, const fs::path &root) {
  const auto documents = root / "documents-terminal";
  fs::create_directories(documents);
  auto assets = std::make_shared<SchoolDays::Assets>(root);
  Application app(assets, route, documents);

  bool foundTerminal = false;
  // This fixture starts from a genuinely fresh route/global state. Following
  // the first legal choice at each prompt reaches the authenticated terminal
  // after 209 bridge advances, so keep a small amount of headroom without
  // depending on state warmed by other bridge-test scenarios.
  for (int step = 0; step < 256 && !foundTerminal; ++step) {
    SchoolDays::RuntimeHandoffTestAccess::AdvanceDeterministic(app.runtime);
    if (!SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime)) continue;
    if (SchoolDays::RuntimeHandoffTestAccess::PendingTerminal(app.runtime)) {
      foundTerminal = true;
      break;
    }
    SchoolDays::RuntimeHandoffTestAccess::OpenDeferred(app.runtime);
    SchoolDays::RuntimeHandoffTestAccess::SubmitSaveFeel(
        app.runtime, Kotonoha::SchoolDaysSaveFeelModel::Result::Skip);
  }
  Require(foundTerminal, "deterministic route did not reach terminal handoff");
  Require(SchoolDays::RuntimeHandoffTestAccess::Deferred(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::HasGameplay(app.runtime),
          "terminal handoff did not defer while retaining outgoing Gameplay");

  SchoolDays::RuntimeHandoffTestAccess::OpenDeferred(app.runtime);
  SchoolDays::RuntimeHandoffTestAccess::SubmitSaveFeel(
      app.runtime, Kotonoha::SchoolDaysSaveFeelModel::Result::Skip);
  Require(!SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime) &&
              !SchoolDays::RuntimeHandoffTestAccess::HasGameplay(app.runtime) &&
              SchoolDays::RuntimeHandoffTestAccess::Idle(app.runtime) &&
              app.runtime.GetState() == Kotonoha::GameState::Menu,
          "terminal acknowledgement did not release Gameplay before Title");
  const auto saveDirectory = documents / "Save";
  const auto globalPath = saveDirectory / "GlobalFlag.DAT";
  Require(fs::is_regular_file(globalPath),
          "Skip at terminal did not flush GlobalFlag");
  std::ifstream in(globalPath, std::ios::binary);
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
  Kotonoha::SchoolDaysFlgH parsed;
  std::string parseError;
  Require(Kotonoha::SchoolDaysPersistenceCodec::ParseFlgH(
              bytes, &parsed, &parseError), parseError);
  Require(std::any_of(parsed.entries.begin(), parsed.entries.end(),
                      [](const auto &entry) {
                        return entry.key == u"EndClear" && entry.integer != 0;
                      }),
          "Skip at terminal lost EndClear from GlobalFlag");
  for (const auto &file : fs::directory_iterator(saveDirectory))
    Require(file.path().filename().string().rfind("SaveFile", 0) != 0,
            "Skip at terminal unexpectedly created a SaveFile slot");
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 4 || argc == 5) {
      const std::string locale = argc == 5 ? argv[4] : "en";
      if (locale != "en") {
        const std::string settings = (fs::path(argv[2]) / "settings.json").string();
        const Kotonoha::SchoolDaysTranslatedGpkSelection selection{
            locale,
            Kotonoha::SchoolDaysGpkLanguageProfile::ScriptPath(settings, locale),
            Kotonoha::SchoolDaysGpkLanguageProfile::LxhPath(settings, locale)};
        std::string error;
        Require(Kotonoha::ConfigureSchoolDaysTranslatedGpks(selection, &error), error);
      }
      auto assets = std::make_shared<SchoolDays::Assets>(argv[2]);
      Application app(assets, argv[1], argv[3],
                      locale == "en"
                          ? fs::path(argv[2]) / "translations/jsons/en-lxh.json"
                          : fs::path{}, locale == "en" ? "" : locale);
      const std::array<const char *, 7> scenes{{
          "05/05-A2-Z00", "05/05-SB-E01", "05/05-B0-F00",
          "05/05-A2-E00", "05/05-SC-L03", "05/05-G0-L03",
          "05/05-KC-F00"}};
      for (int slot = 0; slot < 7; ++slot) {
        SchoolDays::RuntimeHandoffTestAccess::LoadSlot(app.runtime, slot);
        app.runtime.update();
        Require(app.runtime.debugNode() == scenes[slot] &&
                    app.runtime.GetState() == Kotonoha::GameState::Gameplay &&
                    SchoolDays::RuntimeHandoffTestAccess::LxhFadeEnabled(app.runtime) ==
                        (slot != 3) &&
                    SchoolDays::RuntimeHandoffTestAccess::NextTick(app.runtime) > 0u &&
                    !SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime),
                "Ending LOAD returned to menu or lost its LxH timeline");
        std::cout << "LXH_RUNTIME_LOAD " << slot << " scene="
                  << app.runtime.debugNode() << " state="
                  << static_cast<int>(app.runtime.GetState()) << " fade="
                  << SchoolDays::RuntimeHandoffTestAccess::LxhFadeEnabled(app.runtime)
                  << " next=" << SchoolDays::RuntimeHandoffTestAccess::NextTick(app.runtime)
                  << " pending=" << SchoolDays::RuntimeHandoffTestAccess::Pending(app.runtime)
                  << '\n';
      }
      std::string generatedScene;
#if defined(KOTONOHA_DISTRIBUTION_BUILD)
      // Slot 6 is a real saved ending. Re-saving it verifies a newly created
      // slot's LxH timeline without a development-only scene jump.
      generatedScene = scenes[6];
#else
      app.runtime.debugJumpToNode("05/05-A2-F00");
      generatedScene = "05/05-A2-F00";
#endif
      SchoolDays::RuntimeHandoffTestAccess::SaveSlot(app.runtime, 7);
      SchoolDays::RuntimeHandoffTestAccess::LoadSlot(app.runtime, 7);
      Require(app.runtime.debugNode() == generatedScene &&
                  app.runtime.GetState() == Kotonoha::GameState::Gameplay &&
                  SchoolDays::RuntimeHandoffTestAccess::LxhFadeEnabled(app.runtime),
              "Generated seventh ending save did not reopen its LxH timeline");
      std::cout << "LXH_RUNTIME_LOAD 7 scene=" << generatedScene
                << " generated=1\n";
      return 0;
    }
    Require(argc == 3,
            "Usage: SchoolDaysHandoffRuntimeTest route fixture-root");
    TemporaryDirectory temporary;
    PrepareAssets(argv[2], temporary.path);
    TestOrdinaryCommentEscape(argv[1], temporary.path);
    TestMainMenuConfirmNarrative(argv[1], temporary.path);
    TestMainMenuConfirmTemporary(argv[1], temporary.path);
    TestMissingExitPopupDoesNotQuit(argv[1], temporary.path);
    TestFrontendTransitions(argv[1], temporary.path);
    TestSaveFeelAndSaveLoad(argv[1], temporary.path);
    TestExplicitSkip(argv[1], temporary.path);
    TestTimeoutSkip(argv[1], temporary.path);
    TestTerminalRelease(argv[1], temporary.path);
    std::cout << "SchoolDaysHandoffRuntimeTest PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "SchoolDaysHandoffRuntimeTest: " << error.what() << '\n';
    return 1;
  }
}
