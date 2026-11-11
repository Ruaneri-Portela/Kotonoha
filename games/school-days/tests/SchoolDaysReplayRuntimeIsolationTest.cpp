#include <SchoolDays/SchoolDaysAssets.hpp>
#include "SchoolDaysRuntime.hpp"
#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "SchoolDaysReplayRuntimeIsolationTest: " << message << '\n';
  std::exit(2);
}

void Check(bool value, const std::string &message) {
  if (!value)
    Fail(message);
}

bool SameHistory(const std::vector<Kotonoha::SchoolDaysHistory::Entry> &a,
                 const std::vector<Kotonoha::SchoolDaysHistory::Entry> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!(a[i].source == b[i].source) ||
        !(a[i].destination == b[i].destination) ||
        a[i].inverseFeeling != b[i].inverseFeeling)
      return false;
  }
  return true;
}

bool SameBackLog(
    const std::vector<Kotonoha::SchoolDaysBackLogStore::Entry> &a,
    const std::vector<Kotonoha::SchoolDaysBackLogStore::Entry> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].nativeType != b[i].nativeType ||
        a[i].historyIndex != b[i].historyIndex ||
        a[i].character != b[i].character || a[i].text != b[i].text)
      return false;
  }
  return true;
}

void WriteBytes(const std::filesystem::path &path,
                const std::vector<uint8_t> &bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  Check(bool(output), "could not write PlayData isolation fixture");
}

struct Snapshot {
  std::string sceneKey;
  Kotonoha::Gameplay *gameplay = nullptr;
  std::unordered_map<std::string, int64_t> sessionState;
  std::unordered_map<std::string, int64_t> globalState;
  std::vector<uint8_t> saveLog;
  std::vector<uint8_t> globalFlag;
  bool hasPresented = false;
  Kotonoha::SchoolDaysCoordinate presented;
  std::vector<Kotonoha::SchoolDaysHistory::Entry> history;
  std::vector<Kotonoha::SchoolDaysBackLogStore::Entry> backlog;
};

} // namespace

namespace SchoolDays {

class RuntimeReplayIsolationTestAccess {
public:
  static Snapshot Capture(Runtime &runtime) {
    Snapshot out;
    auto &session = runtime.bridge.Session();
    Kotonoha_KtrfError error{};
    out.sceneKey = runtime.bridge.CurrentScene().sceneKey;
    out.gameplay = runtime.bridge.CurrentGameplay();
    Check(session.State().EnumerateSessionInts(&out.sessionState, &error),
          error.message);
    Check(session.State().EnumerateGlobalInts(&out.globalState, &error),
          error.message);
    Check(session.Persistence().SerializeSaveLog(&out.saveLog, &error),
          error.message);
    Check(session.SerializeGlobalFlag(&out.globalFlag, &error), error.message);
    out.hasPresented = session.History().HasPresented();
    out.presented = session.History().Presented();
    out.history = session.History().Entries();
    out.backlog = runtime.backlogStore.Entries();
    return out;
  }

  static void Run(Runtime &runtime) {
    auto &session = runtime.bridge.Session();
    Kotonoha_KtrfError error{};
    std::vector<uint8_t> emptyGlobal;
    std::string codecError;
    Check(Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
              Kotonoha::SchoolDaysFlgH{}, &emptyGlobal, &codecError),
          codecError);
    Check(session.LoadGlobalFlag(emptyGlobal, &error), error.message);
    Check(session.Persistence().WriteGlobalInteger("EndClear", 11, 0,
                                                   &session.State(), &error),
          error.message);

    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult liveForward;
    Check(runtime.bridge.Advance("ktrf:next", &liveForward, &error),
          error.message);
    Check(liveForward.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene,
          "live fixture did not advance to a second physical scene");
    runtime.openScene(liveForward.scene);
    runtime.backlogStore.AppendLogMessage(77, u"Live", u"Keep this message");

    runtime.titleProfile.titleBgmStem = "Music/Title/Fixture";
    runtime.titleProfile.systemBgmStem = "Music/System/Fixture";
    Check(runtime.startFrontendBgm(Runtime::FrontendBgmOwner::System),
          "fixture SystemBGM ownership setup failed");
    Check(runtime.frontendBgmOwner == Runtime::FrontendBgmOwner::System &&
              runtime.frontendBgmStem() == runtime.titleProfile.systemBgmStem,
          "Replay frontend owner did not resolve profile.systemBgmStem");

    const Snapshot before = Capture(runtime);
    runtime.startReplayPlayback(1);
    Check(runtime.temporaryPlaybackActive(), "temporary Replay did not start");
    Check(runtime.frontendBgmOwner == Runtime::FrontendBgmOwner::System &&
              !runtime.titleBgm,
          "temporary Replay did not suspend and preserve SystemBGM ownership");
    Check(runtime.scene.sceneKey == "02/02-2K-J03",
          "ordinary Replay did not bind its authenticated first scene");
    Check(runtime.debugNode() == runtime.scene.sceneKey,
          "debug node did not follow temporary Replay ownership");
    runtime.advance();
    Check(runtime.scene.sceneKey == "02/02-2K-J04",
          "ordinary Replay did not advance inside its private sequence");
    Check(runtime.debugNode() == runtime.scene.sceneKey,
          "debug node did not follow temporary Replay advance");
    runtime.finishTemporaryPlayback(false);
    Check(!runtime.temporaryPlaybackActive(), "temporary Replay did not close");
    Check(runtime.frontendBgmOwner == Runtime::FrontendBgmOwner::Title,
          "return to Title did not restore TitleBGM ownership");
    Check(runtime.debugNode() == before.sceneKey,
          "debug node did not return to live bridge ownership");

    const Snapshot after = Capture(runtime);
    Check(before.sceneKey == after.sceneKey &&
              before.gameplay == after.gameplay,
          "temporary Replay replaced the live bridge scene/Gameplay");
    Check(before.sessionState == after.sessionState &&
              before.globalState == after.globalState,
          "temporary Replay mutated live StateStore");
    Check(before.saveLog == after.saveLog &&
              before.globalFlag == after.globalFlag,
          "temporary Replay mutated live SaveLog/GlobalFlag");
    Check(before.hasPresented == after.hasPresented &&
              before.presented == after.presented &&
              SameHistory(before.history, after.history),
          "temporary Replay mutated live routing history");
    Check(SameBackLog(before.backlog, after.backlog),
          "temporary Replay mutated/truncated live BackLog");

    // Exercise the nested PlayData host against the same retained narrative
    // owner. The private SaveFile may update its own SLog and publish
    // SaveConfig through GlobalFlag, but it must never replace retained
    // scene/session/history.
    const auto temp = std::filesystem::temp_directory_path() /
                      ("school-days-playdata-isolation-" +
                       std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(temp);
    runtime.documents = temp;
    runtime.profile.useEnglish = true;
    runtime.profile.saveFolder = "Save";
    runtime.profile.saveFileName = "Save/SaveFile%d.DAT";
    runtime.profile.flagFileName = "Save/GlobalFlag.DAT";
    runtime.profile.saveConfig = "FILMEngine/SaveFile%d";
    std::string storageError;
    Check(runtime.storage.Initialize(temp, runtime.profile, &storageError),
          storageError);

    std::vector<uint8_t> oracleBytes;
    Check(session.Persistence().SerializeSaveLog(&oracleBytes, &error),
          error.message);
    WriteBytes(temp / "Save/SaveFile0.DAT", oracleBytes);

    const Snapshot retainedBeforePlayData = Capture(runtime);
    runtime.startPlayDataPlayback(0);
    Check(runtime.playDataPlayback && runtime.playDataPlayback->IsOpen(),
          "PlayData isolation fixture did not start");
    runtime.playDataBacklogStore.AppendLogMessage(1, u"Private",
                                                  u"Keep private history");
    const auto *privateGameplay = runtime.playDataPlayback->CurrentGameplay();
    const std::string privateScene =
        runtime.playDataPlayback->CurrentScene().sceneKey;

    runtime.saveLoadOwner = Runtime::SaveLoadOwner::PlayData;
    runtime.saveLoadSaving = true;
    runtime.saveSlot(1, u"private save");
    const Snapshot retainedAfterSave = Capture(runtime);
    Check(retainedBeforePlayData.sceneKey == retainedAfterSave.sceneKey &&
              retainedBeforePlayData.gameplay == retainedAfterSave.gameplay &&
              retainedBeforePlayData.sessionState ==
                  retainedAfterSave.sessionState &&
              retainedBeforePlayData.globalState ==
                  retainedAfterSave.globalState &&
              retainedBeforePlayData.saveLog == retainedAfterSave.saveLog &&
              retainedBeforePlayData.hasPresented ==
                  retainedAfterSave.hasPresented &&
              retainedBeforePlayData.presented == retainedAfterSave.presented &&
              SameHistory(retainedBeforePlayData.history,
                          retainedAfterSave.history) &&
              SameBackLog(retainedBeforePlayData.backlog,
                          retainedAfterSave.backlog),
          "PlayData SAVE contaminated retained narrative "
          "route/session/history/SLog/BackLog");
    Check(retainedBeforePlayData.globalFlag != retainedAfterSave.globalFlag,
          "PlayData SAVE did not publish its committed GlobalFlag image");
    Check(std::filesystem::is_regular_file(temp / "Save/SaveFile1.DAT") &&
              std::filesystem::is_regular_file(temp / "Save/GlobalFlag.DAT"),
          "PlayData SAVE did not write SaveFile then GlobalFlag outputs");

    runtime.saveLoadOwner = Runtime::SaveLoadOwner::PlayData;
    runtime.saveLoadSaving = false;
    const size_t privateBackLogBeforeLoad = runtime.playDataBacklogStore.Size();
    runtime.loadSlot(0);
    Check(runtime.playDataPlayback && runtime.playDataPlayback->IsOpen() &&
              runtime.playDataPlayback->CurrentGameplay() == privateGameplay &&
              runtime.playDataPlayback->CurrentScene().sceneKey ==
                  privateScene &&
              runtime.playDataBacklogStore.Size() == privateBackLogBeforeLoad,
          "armed PlayData LOAD changed private checkpoint/gameplay/BackLog");
    const Snapshot retainedAfterOracleLoad = Capture(runtime);
    Check(retainedAfterSave.sceneKey == retainedAfterOracleLoad.sceneKey &&
              retainedAfterSave.gameplay == retainedAfterOracleLoad.gameplay &&
              retainedAfterSave.sessionState ==
                  retainedAfterOracleLoad.sessionState &&
              retainedAfterSave.globalState ==
                  retainedAfterOracleLoad.globalState &&
              retainedAfterSave.saveLog == retainedAfterOracleLoad.saveLog &&
              retainedAfterSave.globalFlag ==
                  retainedAfterOracleLoad.globalFlag &&
              SameHistory(retainedAfterSave.history,
                          retainedAfterOracleLoad.history) &&
              SameBackLog(retainedAfterSave.backlog,
                          retainedAfterOracleLoad.backlog),
          "armed PlayData LOAD contaminated retained narrative owner");

    runtime.playDataPlayback.reset();
    runtime.playDataBacklogStore.ResetTransient();
    runtime.documents.clear();
    std::error_code ignored;
    std::filesystem::remove_all(temp, ignored);
  }
};

} // namespace SchoolDays

int main(int argc, char **argv) {
  Check(argc == 3,
        "usage: SchoolDaysReplayRuntimeIsolationTest <route> <fixture-root>");
  auto assets = std::make_shared<Kotonoha::Providers::SchoolDaysAssets>(argv[2]);
  SchoolDays::Runtime runtime(assets, argv[1]);
  SchoolDays::RuntimeReplayIsolationTestAccess::Run(runtime);
  std::cout << "SchoolDaysReplayRuntimeIsolationTest PASS\n";
  return 0;
}
