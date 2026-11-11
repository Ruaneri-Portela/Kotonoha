#include <SchoolDays/SchoolDaysAssets.hpp>
#include "SchoolDaysRuntime.hpp"

#include <Kotonoha/core/Engine.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>
#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>
#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {

[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "SchoolDaysReplayRuntimeUiTest: " << message << '\n';
  std::exit(2);
}

void Check(bool value, const std::string &message) {
  if (!value)
    Fail(message);
}

void Put(const fs::path &path, const std::string &data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  Check(bool(out), "cannot write " + path.string());
}

void Put(const fs::path &path, const std::vector<uint8_t> &data) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  Check(bool(out), "cannot write " + path.string());
}

void WriteLe16(std::vector<uint8_t> *bytes, size_t offset, uint16_t value) {
  (*bytes)[offset] = static_cast<uint8_t>(value);
  (*bytes)[offset + 1] = static_cast<uint8_t>(value >> 8u);
}

void WriteLe32(std::vector<uint8_t> *bytes, size_t offset, uint32_t value) {
  (*bytes)[offset] = static_cast<uint8_t>(value);
  (*bytes)[offset + 1] = static_cast<uint8_t>(value >> 8u);
  (*bytes)[offset + 2] = static_cast<uint8_t>(value >> 16u);
  (*bytes)[offset + 3] = static_cast<uint8_t>(value >> 24u);
}

void PutBmp(const fs::path &path, int width, int height) {
  const uint32_t pixels = static_cast<uint32_t>(width * height * 4);
  std::vector<uint8_t> bytes(54u + pixels, 0u);
  bytes[0] = 'B';
  bytes[1] = 'M';
  WriteLe32(&bytes, 2, static_cast<uint32_t>(bytes.size()));
  WriteLe32(&bytes, 10, 54u);
  WriteLe32(&bytes, 14, 40u);
  WriteLe32(&bytes, 18, static_cast<uint32_t>(width));
  WriteLe32(&bytes, 22, static_cast<uint32_t>(height));
  WriteLe16(&bytes, 26, 1u);
  WriteLe16(&bytes, 28, 32u);
  WriteLe32(&bytes, 34, pixels);
  for (size_t i = 54; i + 3 < bytes.size(); i += 4) {
    bytes[i] = 24u;
    bytes[i + 1] = 24u;
    bytes[i + 2] = 24u;
    bytes[i + 3] = 255u;
  }
  Put(path, bytes);
}

struct CmapFixture {
  int width = 1280;
  int height = 720;
  std::vector<uint8_t> bytes;

  explicit CmapFixture(int w = 1280, int h = 720)
      : width(w), height(h),
        bytes(8u + static_cast<size_t>(width) * height, 0u) {
    WriteLe32(&bytes, 0, static_cast<uint32_t>(width));
    WriteLe32(&bytes, 4, static_cast<uint32_t>(height));
  }

  void Rect(int x, int y, int w, int h, uint8_t rawRegion) {
    for (int py = y; py < y + h; ++py)
      for (int px = x; px < x + w; ++px)
        bytes[8u + static_cast<size_t>(py) * width + px] = rawRegion;
  }
};

struct TemporaryDirectory {
  fs::path path =
      fs::temp_directory_path() /
      ("school-days-replay-ui-" + std::to_string(SDL_GetPerformanceCounter()));
  TemporaryDirectory() {
    Check(fs::create_directory(path), "cannot create temporary directory");
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};

Kotonoha::SchoolDaysFlgHEntry IntegerEntry(const char *key, int32_t value) {
  Kotonoha::SchoolDaysFlgHEntry entry;
  while (*key)
    entry.key.push_back(static_cast<char16_t>(*key++));
  entry.type = 11;
  entry.integer = value;
  return entry;
}

Kotonoha::SchoolDaysFlgHEntry StringEntry(const char16_t *key,
                                          const char16_t *value) {
  Kotonoha::SchoolDaysFlgHEntry entry;
  entry.key = key;
  entry.type = 8;
  entry.stringValue = value;
  return entry;
}

void WriteFixtures(const fs::path &root, const fs::path &documents) {
  Put(root / "Script/English/00/00-00-A00.ENG.ORS",
      "[PrintText]=00:00:00\tFixture\tReplay UI fixture.\t00:01:00;\n"
      "[SkipFRAME]=00:01:00;\n"
      "[Next]=00:02:00;\n");

  Put(root / "ini/FILMENGINE.INI", "[UseEnglish]=\"1\"\n"
                                   "[TextInput]=\"1\"\n"
                                   "[FontData]=\"System/FONTDATA.DAT\"\n"
                                   "[SaveFolder]=\"Save\"\n"
                                   "[SaveFileName]=\"Save/SaveFile%d.DAT\"\n"
                                   "[FlagFileName]=\"Save/GlobalFlag.DAT\"\n"
                                   "[SaveConfig]=\"FILMEngine/SaveFile%d\"\n"
                                   "[SeCancel]=\"Music/System/Cancel\"\n"
                                   "[SeSelect]=\"Music/System/Select\"\n"
                                   "[SeClick]=\"Music/System/Click\"\n"
                                   "[SeUp]=\"Music/System/Up\"\n"
                                   "[SeDown]=\"Music/System/Down\"\n"
                                   "[SeView]=\"Music/System/View\"\n"
                                   "[SeOpen]=\"Music/System/Open\"\n");

  CmapFixture title;
  title.Rect(540, 550, 220, 100, 3); // title item 2: REPLAY
  Put(root / "System/Title/Title.cmap", title.bytes);
  Put(root / "System/Title/Title_Wide_Full.cmap", title.bytes);

  CmapFixture hscene;
  hscene.Rect(300, 70, 300, 100, 2); // control 1: PlayData tab
  Put(root / "System/Replay/Replay_HScene.cmap", hscene.bytes);
  Put(root / "System/Replay/Replay_HScene_Wide_Full.cmap", hscene.bytes);

  CmapFixture playData;
  playData.Rect(680, 60, 100, 80, 15); // control 14: page 1
  playData.Rect(80, 160, 500, 90, 24); // control 23: row 0
  Put(root / "System/Replay/Replay_PlayData.cmap", playData.bytes);
  Put(root / "System/Replay/Replay_PlayData_Wide_Full.cmap", playData.bytes);

  PutBmp(root / "System/Replay/Replay_HScene.png", 800, 450);
  PutBmp(root / "System/Replay/Replay_HScene_Chip.png", 800, 450);
  PutBmp(root / "System/Replay/Replay_Thm01.png", 660, 570);
  PutBmp(root / "System/Replay/Replay_PlayData.png", 800, 450);
  PutBmp(root / "System/Replay/Replay_PlayData_Chip.png", 800, 450);

  CmapFixture saveLoad;
  saveLoad.Rect(80, 160, 500, 90, 1); // slot row 0
  Put(root / "System/SaveLoad/SaveLoad.cmap", saveLoad.bytes);
  Put(root / "System/SaveLoad/SaveLoad_Wide_Full.cmap", saveLoad.bytes);
  PutBmp(root / "System/SaveLoad/Save.png", 800, 450);
  PutBmp(root / "System/SaveLoad/Load.png", 800, 450);
  PutBmp(root / "System/SaveLoad/SaveLoad_Chip.png", 800, 450);

  CmapFixture backlog;
  CmapFixture standardBacklog(800, 600);
  Put(root / "System/BackLog/BackLog_Horizon.cmap", standardBacklog.bytes);
  Put(root / "System/BackLog/BackLog_Horizon_Wide_Full.cmap", backlog.bytes);
  PutBmp(root / "System/BackLog/BackLog_Horizon.png", 800, 450);
  PutBmp(root / "System/BackLog/BackLog_Horizon_Chip.png", 800, 450);
  Put(root / "System/FONTDATA_eng.DAT",
      std::vector<uint8_t>(Kotonoha::SchoolDaysFontData::kTableBytes, 0u));

  Kotonoha::SchoolDaysFlgH global;
  global.entries.push_back(IntegerEntry("EndClear", -1));
  for (int i = 0; i < Kotonoha::SchoolDaysReplayModel::HSceneUnlockCount(); ++i)
    global.entries.push_back(
        IntegerEntry(Kotonoha::SchoolDaysReplayModel::HSceneUnlockKey(i), -1));
  global.entries.push_back(
      StringEntry(u"FILMEngine/SaveFile10", u"2026/11/01 07:2205"));
  global.entries.push_back(
      StringEntry(u"FILMEngine/SaveFile10_Sub", u"Replay slot ten"));
  std::vector<uint8_t> globalBytes;
  std::string error;
  Check(Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
            global, &globalBytes, &error),
        error);
  Put(documents / "Save/GlobalFlag.DAT", globalBytes);
}

std::vector<uint8_t> BuildPlayDataSave(const char *route,
                                       const fs::path &root) {
  Kotonoha::SchoolDaysKtrfSession source;
  Kotonoha_KtrfError error{};
  Check(source.Open(route, (root / "Script/English").string().c_str(), &error),
        error.message);
  Kotonoha::SchoolDaysKtrfScene scene;
  Check(source.ResetNewGame(&scene, &error), error.message);
  Check(source.Persistence().ReplaceCurrentScriptSnapshot(
            scene.sceneKey, UINT32_C(0x3f800000), source.State(), &error),
        error.message);
  std::vector<uint8_t> bytes;
  Check(source.Persistence().SerializeSaveLog(&bytes, &error), error.message);
  source.Close();
  return bytes;
}

Kotonoha::EngineOptions
Options(const std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> &assets) {
  Kotonoha::EngineOptions options;
  options.assets = assets;
  options.persistSettings = false;
  options.rendererOverride = "software";
  options.settings.mode = Kotonoha::WindowMode::Borderless;
  return options;
}

struct Application {
  SchoolDays::Runtime runtime;
  std::unique_ptr<Kotonoha::Engine> engine;

  Application(const std::shared_ptr<Kotonoha::Providers::SchoolDaysAssets> &assets,
              const char *route, const fs::path &documents)
      : runtime(assets, route, documents) {
    runtime.SetState(Kotonoha::GameState::Menu);
    engine = std::make_unique<Kotonoha::Engine>(runtime, Options(assets));
  }
  ~Application() { runtime.shutdown(); }
};

void Motion(SchoolDays::Runtime &runtime, float x, float y) {
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.x = x;
  event.motion.y = y;
  runtime.onEvent(event);
}

void TitleClick(SchoolDays::Runtime &runtime, float x, float y) {
  Motion(runtime, x, y);
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  runtime.onEvent(event);
}

void PanelClick(SchoolDays::Runtime &runtime, float x, float y) {
  Motion(runtime, x, y);
  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = x;
  event.button.y = y;
  runtime.onEvent(event);
}

void RenderMenu(Application &app, const std::string &stage) {
  app.runtime.renderMenu(app.engine->GetRenderer(), 1.0f);
  Check(app.runtime.panels().IsReplayOpen(),
        "Replay presenter closed while rendering " + stage);
}

} // namespace

namespace SchoolDays {

class RuntimeReplayUiTestAccess {
public:
  static bool HasSystemBgmOwner(const Runtime &runtime) {
    return runtime.frontendBgmOwner == Runtime::FrontendBgmOwner::System;
  }

  static bool HasTitleBgmOwner(const Runtime &runtime) {
    return runtime.frontendBgmOwner == Runtime::FrontendBgmOwner::Title;
  }

  static void FinishToReplay(Runtime &runtime) {
    runtime.finishTemporaryPlayback(true);
  }

  static void FinishInitialTitleFade(Runtime &runtime) {
    runtime.updateFrontendTransition(static_cast<uint64_t>(SDL_GetTicks()) +
                                     1000u);
  }

  static void FinishTitleChildFade(Runtime &runtime) {
    runtime.updateFrontendTransition(static_cast<uint64_t>(SDL_GetTicks()) +
                                     500u);
  }

  static void FinishFrontendIncoming(Runtime &runtime) {
    runtime.updateFrontendTransition(static_cast<uint64_t>(SDL_GetTicks()) +
                                     1000u);
  }

  static void ReturnToTitle(Runtime &runtime) { runtime.openTitle(); }

  static bool PlayDataOpen(const Runtime &runtime) {
    return runtime.playDataPlayback && runtime.playDataPlayback->IsOpen();
  }

  static bool ReplayDataLoading(const Runtime &runtime) {
    return runtime.loadTransition.Active() &&
           runtime.loadTransition.Target() ==
               Kotonoha::SchoolDaysLoadTransition::Destination::ReplayData;
  }

  static void CompleteReplayDataLoading(Runtime &runtime) {
    // Simulate the original observed card phases without introducing a
    // multi-second sleep into the UI regression test.
    const uint64_t t = SDL_GetTicks();
    runtime.updateLoadTransition(t + 1000u);
    runtime.updateLoadTransition(t + 1900u);
    runtime.updateLoadTransition(t + 2400u);
    runtime.updateLoadTransition(t + 2900u);
  }

  static bool PlayDataMenuBarProjectionExact(const Runtime &runtime) {
    const auto projection = runtime.menuBarProjection();
    return projection.speedControlsEnabled && !projection.transportLocked &&
           !projection.actionInhibit && !projection.fullBarSuppressed &&
           projection.replayIndicator;
  }

  static Kotonoha::Gameplay *PlayDataGameplay(const Runtime &runtime) {
    return runtime.playDataPlayback
               ? runtime.playDataPlayback->CurrentGameplay()
               : nullptr;
  }

  static std::string PlayDataScene(const Runtime &runtime) {
    return runtime.playDataPlayback
               ? runtime.playDataPlayback->CurrentScene().sceneKey
               : std::string();
  }

  static void SetPaused(Runtime &runtime, bool value) {
    runtime.paused = value;
  }
  static bool Paused(const Runtime &runtime) { return runtime.paused; }

  static void OpenSave(Runtime &runtime) { runtime.openSaveLoad(true); }
  static void OpenLoad(Runtime &runtime) { runtime.openSaveLoad(false); }
  static void OpenBackLog(Runtime &runtime) { runtime.openBacklog(); }

  static bool SaveLoadOwnedByPlayData(const Runtime &runtime) {
    return runtime.saveLoadOwner == Runtime::SaveLoadOwner::PlayData;
  }

  static bool BackLogOwnedByPlayData(const Runtime &runtime) {
    return runtime.backlogOwner == Runtime::BackLogOwner::PlayData &&
           runtime.backlogModel.Store() == &runtime.playDataBacklogStore;
  }

  static void AppendNarrativeBackLog(Runtime &runtime, int index,
                                     const char16_t *text) {
    runtime.backlogStore.AppendLogMessage(index, u"Narrative", text);
  }

  static void AppendPlayDataBackLog(Runtime &runtime, int index,
                                    const char16_t *text) {
    runtime.playDataBacklogStore.AppendLogMessage(index, u"PlayData", text);
  }

  static size_t NarrativeBackLogSize(const Runtime &runtime) {
    return runtime.backlogStore.Size();
  }

  static size_t PlayDataBackLogSize(const Runtime &runtime) {
    return runtime.playDataBacklogStore.Size();
  }

  static void SaveLoadAction(Runtime &runtime,
                             Kotonoha::SchoolDaysSaveLoadModel::Action action,
                             int slot = -1,
                             const std::u16string &comment = {}) {
    Kotonoha::SchoolDaysSystemUi::Output output;
    output.hasSaveLoadOutput = true;
    output.saveLoadOutput.action = action;
    output.saveLoadOutput.slot = slot;
    output.saveLoadOutput.comment = comment;
    runtime.handlePanel(output);
  }

  static int SaveLoadPageRefreshMarker(const Runtime &runtime) {
    return runtime.saveLoadPage;
  }

  static const std::string &Status(const Runtime &runtime) {
    return runtime.status;
  }
};

} // namespace SchoolDays

int main(int argc, char **argv) {
  Check(argc == 2, "usage: SchoolDaysReplayRuntimeUiTest <route>");
  TemporaryDirectory temporary;
  const fs::path root = temporary.path / "assets";
  const fs::path documents = temporary.path / "documents";
  WriteFixtures(root, documents);
  Put(documents / "Save/SaveFile10.DAT", BuildPlayDataSave(argv[1], root));

  auto assets = std::make_shared<Kotonoha::Providers::SchoolDaysAssets>(root);
  Application app(assets, argv[1], documents);
  Check(app.runtime.GetState() == Kotonoha::GameState::Menu,
        "runtime did not initialize at title");

  SchoolDays::RuntimeReplayUiTestAccess::FinishInitialTitleFade(app.runtime);
  TitleClick(app.runtime, 600.0f, 590.0f);
  Check(
      !app.runtime.panels().IsReplayOpen() &&
          SchoolDays::RuntimeReplayUiTestAccess::HasTitleBgmOwner(app.runtime),
      "Title->Replay changed child/BGM ownership before the 500ms fade "
      "handoff");
  SchoolDays::RuntimeReplayUiTestAccess::FinishTitleChildFade(app.runtime);
  Check(app.runtime.panels().IsReplayOpen(),
        "100% title REPLAY activation did not open SceneView");
  Check(app.runtime.panels().Replay().CurrentView() ==
                Kotonoha::SchoolDaysReplayModel::View::HScene &&
            app.runtime.panels().Replay().IsHSceneAvailable(0) &&
            app.runtime.panels().Replay().IsHSceneAvailable(40),
        "HScene unlock globals were not projected into Replay UI");
  Check(SchoolDays::RuntimeReplayUiTestAccess::HasSystemBgmOwner(app.runtime),
        "Replay frontend did not take SystemBGM ownership");
  SchoolDays::RuntimeReplayUiTestAccess::FinishFrontendIncoming(app.runtime);

  SchoolDays::RuntimeReplayUiTestAccess::FinishToReplay(app.runtime);
  Check(
      app.runtime.panels().IsReplayOpen() &&
          SchoolDays::RuntimeReplayUiTestAccess::HasSystemBgmOwner(app.runtime),
      "temporary playback return did not restore Replay SystemBGM ownership");
  RenderMenu(app, "HScene page 0");

  PanelClick(app.runtime, 400.0f, 110.0f);
  Check(app.runtime.panels().Replay().CurrentView() ==
                Kotonoha::SchoolDaysReplayModel::View::PlayData &&
            app.runtime.panels().Replay().Page() == 0,
        "Replay did not navigate to PlayData");
  RenderMenu(app, "PlayData page 0");

  PanelClick(app.runtime, 720.0f, 100.0f);
  Check(app.runtime.panels().Replay().Page() == 1,
        "PlayData page navigation did not select page 1");
  Motion(app.runtime, 200.0f, 200.0f);
  Check(app.runtime.panels().Replay().Selection() == 23 &&
            app.runtime.panels().Replay().IsPlayDataAvailable(10),
        "SaveStorage slot 10 did not surface as an available Replay Data row");
  RenderMenu(app, "PlayData page 1 metadata row");

  PanelClick(app.runtime, 200.0f, 200.0f);
  using Access = SchoolDays::RuntimeReplayUiTestAccess;
  using SaveAction = Kotonoha::SchoolDaysSaveLoadModel::Action;
  Check(
      Access::ReplayDataLoading(app.runtime) &&
          !Access::PlayDataOpen(app.runtime) &&
          app.runtime.panels().IsReplayOpen(),
      "Replay Data must show retail loading card before constructing PlayData");
  Access::CompleteReplayDataLoading(app.runtime);
  Check(Access::PlayDataOpen(app.runtime) &&
            !app.runtime.panels().IsReplayOpen(),
        "Replay Data row did not start isolated PlayData playback");
  Check(Access::PlayDataMenuBarProjectionExact(app.runtime),
        "PlayData MenuBar projection did not expose Save/Load/BackLog + replay "
        "indicator");

  auto *privateGameplay = Access::PlayDataGameplay(app.runtime);
  const std::string privateScene = Access::PlayDataScene(app.runtime);
  Access::SetPaused(app.runtime, true);
  Access::OpenSave(app.runtime);
  Check(app.runtime.panels().IsSaveLoadOpen() &&
            Access::SaveLoadOwnedByPlayData(app.runtime),
        "PlayData SAVE did not open the retail SaveLoad host");
  Access::SaveLoadAction(app.runtime, SaveAction::Close);
  Check(
      !app.runtime.panels().IsSaveLoadOpen() &&
          Access::PlayDataOpen(app.runtime) &&
          Access::PlayDataGameplay(app.runtime) == privateGameplay &&
          Access::PlayDataScene(app.runtime) == privateScene &&
          Access::Paused(app.runtime),
      "closing nested PlayData SaveLoad changed playback owner or user pause");
  Access::SetPaused(app.runtime, false);

  Access::OpenLoad(app.runtime);
  Check(app.runtime.panels().IsSaveLoadOpen() &&
            Access::SaveLoadOwnedByPlayData(app.runtime),
        "PlayData LOAD did not open the retail SaveLoad host");
  Access::SaveLoadAction(app.runtime, SaveAction::RouteMap);
  Check(app.runtime.panels().IsSaveLoadOpen() &&
            !app.runtime.panels().IsRouteMapOpen() &&
            Access::SaveLoadOwnedByPlayData(app.runtime),
        "nested PlayData LOAD leaked into RouteMap");
  Access::SaveLoadAction(app.runtime, SaveAction::LoadSlotRequested, 99);
  Check(app.runtime.panels().IsSaveLoadOpen() &&
            Access::SaveLoadOwnedByPlayData(app.runtime),
        "invalid nested PlayData LOAD closed its modal");

  Access::AppendPlayDataBackLog(app.runtime, 1,
                                u"private before oracle-only load");
  const size_t privateBackLogBeforeLoad =
      Access::PlayDataBackLogSize(app.runtime);
  Access::SaveLoadAction(app.runtime, SaveAction::LoadSlotRequested, 10);
  Check(!app.runtime.panels().IsSaveLoadOpen() &&
            Access::PlayDataGameplay(app.runtime) == privateGameplay &&
            Access::PlayDataScene(app.runtime) == privateScene &&
            Access::PlayDataBackLogSize(app.runtime) ==
                privateBackLogBeforeLoad,
        "armed PlayData LOAD changed checkpoint/gameplay or reset private "
        "BackLog");

  Access::AppendNarrativeBackLog(app.runtime, 7, u"retained narrative entry");
  const size_t narrativeBackLogBefore =
      Access::NarrativeBackLogSize(app.runtime);
  Access::OpenBackLog(app.runtime);
  Check(app.runtime.backlogOpen() &&
            Access::BackLogOwnedByPlayData(app.runtime),
        "PlayData BackLog did not bind the private store");
  Access::SaveLoadAction(app.runtime, SaveAction::None);
  SDL_Event escape{};
  escape.type = SDL_EVENT_KEY_DOWN;
  escape.key.key = SDLK_ESCAPE;
  app.runtime.onEvent(escape);
  Check(!app.runtime.backlogOpen() &&
            Access::NarrativeBackLogSize(app.runtime) == narrativeBackLogBefore,
        "PlayData BackLog close mutated retained narrative BackLog");

  Access::OpenSave(app.runtime);
  Check(app.runtime.panels().IsSaveLoadOpen() &&
            Access::SaveLoadOwnedByPlayData(app.runtime),
        "PlayData SAVE reopen failed");
  Motion(app.runtime, 120.0f, 180.0f);
  PanelClick(app.runtime, 120.0f, 180.0f);
  Check(app.runtime.panels().IsSaveLoadCommentEditorOpen(),
        "PlayData SAVE row did not enter async comment editor");
  app.runtime.onEvent(escape);
  Check(!app.runtime.panels().IsSaveLoadCommentEditorOpen() &&
            app.runtime.panels().IsSaveLoadOpen() &&
            Access::SaveLoadOwnedByPlayData(app.runtime),
        "comment Esc closed the outer PlayData SAVE modal");
  Access::SaveLoadAction(app.runtime, SaveAction::SaveSlotRequested, 11,
                         u"PlayData private save");
  Check(app.runtime.panels().IsSaveLoadOpen() &&
            Access::SaveLoadOwnedByPlayData(app.runtime),
        "PlayData SAVE closed its modal after slot request: " +
            Access::Status(app.runtime));
  Check(Access::SaveLoadPageRefreshMarker(app.runtime) == -1,
        "PlayData SAVE did not invalidate metadata page: " +
            Access::Status(app.runtime));
  Check(fs::is_regular_file(documents / "Save/SaveFile11.DAT"),
        "PlayData SAVE did not write SaveFile11: " +
            Access::Status(app.runtime));

  SchoolDays::RuntimeReplayUiTestAccess::ReturnToTitle(app.runtime);
  Check(
      !app.runtime.panels().IsReplayOpen() &&
          SchoolDays::RuntimeReplayUiTestAccess::HasTitleBgmOwner(app.runtime),
      "return to Title did not restore TitleBGM ownership");

  std::cout << "SchoolDaysReplayRuntimeUiTest PASS\n";
  return 0;
}
