#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>
#include <Kotonoha/routing/SchoolDaysPersistenceCodec.hpp>
#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>
#include <Kotonoha/routing/SchoolDaysSaveStorage.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Resolver = Kotonoha::SchoolDaysEndBackgroundResolver;
using Session = Kotonoha::SchoolDaysKtrfSession;

void Check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct Scratch {
  fs::path root;

  Scratch() {
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt != 100; ++attempt) {
      root = fs::temp_directory_path() /
             ("school-days-allclear-synthetic-" + std::to_string(tick) + "-" +
              std::to_string(attempt));
      if (fs::create_directory(root))
        return;
    }
    throw std::runtime_error("Cannot create isolated AllClear QA directory");
  }

  ~Scratch() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

std::vector<uint8_t> ReadFile(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  Check(bool(stream), "Cannot read synthetic GlobalFlag from disk");
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> Serialize(Session &session) {
  Kotonoha_KtrfError error{};
  std::vector<uint8_t> bytes;
  Check(session.SerializeGlobalFlag(&bytes, &error),
        std::string("SerializeGlobalFlag: ") + error.message);
  return bytes;
}

int64_t Global(const Session &session, const char *key) {
  Kotonoha_KtrfError error{};
  int64_t value = 0;
  Check(session.State().ReadGlobalInt(key, &value, &error),
        std::string("ReadGlobalInt ") + key + ": " + error.message);
  return value;
}

void Set(Session &session, const std::string &key, int32_t type,
         int32_t value) {
  Kotonoha_KtrfError error{};
  Check(session.Persistence().WriteGlobalInteger(
            key, type, value, &session.State(), &error),
        std::string("WriteGlobalInteger ") + key + ": " + error.message);
}

Resolver::Profile Profile(int count = 22) {
  Resolver::Profile profile;
  profile.endBgView = true;
  profile.endingMax = count;
  profile.baseAsset = "synthetic-base.png";
  profile.allClearAsset = "synthetic-all.png";
  for (int i = 0; i < count; ++i)
    profile.endingAssets.push_back("synthetic-end-" + std::to_string(i) +
                                   ".png");
  return profile;
}

Resolver::Result Resolve(const Resolver::Profile &profile, Session &session) {
  Resolver::Result result;
  Kotonoha_KtrfError error{};
  Check(Resolver::Resolve(profile, &session.State(), &result, &error),
        std::string("End-background Resolve: ") + error.message);
  return result;
}

void OpenFresh(Session &session, const char *route, const char *assets) {
  Kotonoha_KtrfError error{};
  Check(session.Open(route, assets, &error),
        std::string("Open synthetic session: ") + error.message);
  Kotonoha::SchoolDaysKtrfScene firstScene;
  Check(session.ResetNewGame(&firstScene, &error),
        std::string("ResetNewGame: ") + error.message);
  Kotonoha::SchoolDaysFlgH empty;
  std::vector<uint8_t> bytes;
  std::string codecError;
  Check(Kotonoha::SchoolDaysPersistenceCodec::SerializeFlgH(
            empty, &bytes, &codecError),
        "Cannot serialize empty synthetic FlgH: " + codecError);
  Check(session.LoadGlobalFlag(bytes, &error),
        std::string("Load empty synthetic FlgH: ") + error.message);
}

void TestConfiguredCount(const char *route, const char *assets) {
  Session session;
  OpenFresh(session, route, assets);
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfScene scene;
  Check(session.CurrentScene(&scene, &error), error.message);
  auto profile = Profile(2);
  Check(session.Persistence().RegisterEnding(
            0, scene.sceneKey, &session.State(), &error),
        error.message);
  Check(Resolve(profile, session).kind == Resolver::Kind::Ending,
        "N-1 of a configured EndingMax=2 selected AllClear");
  Check(session.Persistence().RegisterEnding(
            1, scene.sceneKey, &session.State(), &error),
        error.message);
  const auto full = Resolve(profile, session);
  Check(full.kind == Resolver::Kind::AllClear && full.promotedAllClear,
        "Configured EndingMax=2 was not promoted at N=2");
}

void TestAllClear(const char *route, const char *assets) {
  Session session;
  OpenFresh(session, route, assets);
  Kotonoha_KtrfError error{};
  Kotonoha::SchoolDaysKtrfScene scene;
  Check(session.CurrentScene(&scene, &error), error.message);
  auto profile = Profile();

  // EndClear gates the count. EndBGView=0 bypasses all global reads, including
  // malformed EndingMax; this is intentionally tested before any ending.
  Check(Resolve(profile, session).kind == Resolver::Kind::Base,
        "EndClear=0 exposed an ending");
  auto disabled = profile;
  disabled.endBgView = false;
  disabled.endingMax = 999;
  disabled.endingAssets.clear();
  Check(Resolve(disabled, session).kind == Resolver::Kind::Base,
        "EndBGView=0 did not bypass EndingMax validation");

  for (int ending = 0; ending < 21; ++ending) {
    Check(session.Persistence().RegisterEnding(
              ending, scene.sceneKey, &session.State(), &error),
          std::string("Register ending: ") + error.message);
  }
  Check(session.Persistence().RegisterEnding(
            0, scene.sceneKey, &session.State(), &error),
        error.message);
  Check(session.Persistence().RegisterEnding(
            20, scene.sceneKey, &session.State(), &error),
        error.message);
  auto result = Resolve(profile, session);
  Check(result.kind == Resolver::Kind::Ending && result.endingIndex == 20 &&
            !result.promotedAllClear && Global(session, "AllClear") == 0,
        "N-1 plus duplicate endings incorrectly promoted AllClear");

  auto invalidProfile = profile;
  invalidProfile.endingMax = 23;
  Check(!Resolver::Resolve(invalidProfile, &session.State(), &result, &error) &&
            error.code == KOTONOHA_KTRF_ERROR_RANGE,
        "EndingMax exceeding configured assets was accepted");
  invalidProfile.endingMax = -1;
  Check(!Resolver::Resolve(invalidProfile, &session.State(), &result, &error) &&
            error.code == KOTONOHA_KTRF_ERROR_RANGE,
        "Negative EndingMax was accepted");
  const auto beforeInvalidEnding = Serialize(session);
  Check(!session.Persistence().RegisterEnding(
            22, scene.sceneKey, &session.State(), &error) &&
            error.code == KOTONOHA_KTRF_ERROR_RANGE &&
            Serialize(session) == beforeInvalidEnding,
        "Out-of-range ending 22 changed persisted progress");

  Set(session, "EndNo", 3, 22);
  Check(!Resolver::Resolve(profile, &session.State(), &result, &error) &&
            error.code == KOTONOHA_KTRF_ERROR_RANGE &&
            Global(session, "AllClear") == 0,
        "EndNo >= EndingMax was accepted for incomplete profile");
  Set(session, "EndNo", 3, -1);
  Check(!Resolver::Resolve(profile, &session.State(), &result, &error) &&
            error.code == KOTONOHA_KTRF_ERROR_RANGE,
        "Negative EndNo was accepted");
  Set(session, "EndNo", 3, 20);

  Check(session.Persistence().RegisterEnding(
            21, scene.sceneKey, &session.State(), &error),
        error.message);
  disabled = profile;
  disabled.endBgView = false;
  result = Resolve(disabled, session);
  Check(result.kind == Resolver::Kind::Base && !result.promotedAllClear &&
            Global(session, "AllClear") == 0,
        "EndBGView=0 promoted a fully-completed profile");
  auto missingAsset = profile;
  missingAsset.allClearAsset.clear();
  Check(!Resolver::Resolve(missingAsset, &session.State(), &result, &error) &&
            error.code == KOTONOHA_KTRF_ERROR_FORMAT &&
            Global(session, "AllClear") == 0,
        "Missing AllClear asset mutated global state");

  const char *replayKey = Kotonoha::SchoolDaysReplayModel::HSceneUnlockKey(0);
  Check(replayKey != nullptr, "H-scene unlock key unavailable");
  bool unlocked = true;
  Check(Kotonoha::SchoolDaysReplayModel::AnyHSceneUnlocked(
            session.State(), &unlocked, &error) &&
            !unlocked,
        "Fresh synthetic profile had Replay unlocked");
  Set(session, replayKey, 11, -1);
  Set(session, "SP100", 11, -1);

  result = Resolve(profile, session);
  Check(result.kind == Resolver::Kind::AllClear && result.promotedAllClear &&
            result.asset == profile.allClearAsset &&
            Global(session, "AllClear") != 0,
        "N=22 did not promote AllClear");
  // Production's Runtime::toTitle persists the resolver's promotion into the
  // FlgH owner: SchoolDaysRuntime.cpp:1694-1703.
  Set(session, "AllClear", 11, -1);
  const auto promoted = Serialize(session);
  result = Resolve(profile, session);
  Check(result.kind == Resolver::Kind::AllClear &&
            !result.promotedAllClear && Serialize(session) == promoted,
        "Second title resolution modified persisted AllClear bytes");
  Check(Global(session, "EndClear") != 0 &&
            Global(session, "EndNo") == 21 &&
            session.Persistence().GlobalIntegerIsSet("SP100"),
        "AllClear promotion altered the ending or route-map unlock");

  Scratch scratch;
  Kotonoha::SchoolDaysSaveProfile storageProfile;
  storageProfile.saveFolder = "Save";
  // The production profile parser accepts exactly one %d token, not printf
  // width specifiers such as %02d (SchoolDaysSaveProfile::FormatIndexed).
  storageProfile.saveFileName = "Save/SaveFile%d.DAT";
  storageProfile.flagFileName = "Save/GlobalFlag.DAT";
  Kotonoha::SchoolDaysSaveStorage storage;
  std::string storageError;
  Check(storage.Initialize(scratch.root, storageProfile, &storageError),
        "Storage initialization failed: " + storageError);
  Check(storage.WriteGlobalFlagBytes(promoted, &storageError),
        "Slotless AllClear flush failed: " + storageError);
  const auto globalPath = scratch.root / "Save" / "GlobalFlag.DAT";
  Check(ReadFile(globalPath) == promoted,
        "Disk GlobalFlag bytes differ from C++ FlgH codec");
  bool occupied = true;
  Check(!fs::exists(scratch.root / "Save" / "SaveFile0.DAT"),
        "Slotless GlobalFlag flush unexpectedly created SaveFile0.DAT");
  Check(storage.ProbeSaveFile(0, &occupied, &storageError),
        "Synthetic slot probe failed: " + storageError);
  Check(!occupied, "Synthetic slot probe reported SaveFile0.DAT occupied");

  std::vector<uint8_t> reloaded;
  bool present = false;
  Check(storage.LoadGlobalFlag(&reloaded, &present, &storageError) &&
            present && reloaded == promoted,
        "GlobalFlag could not be reloaded from the isolated disk");
  Session restarted;
  OpenFresh(restarted, route, assets);
  Check(restarted.LoadGlobalFlag(reloaded, &error),
        std::string("Load persisted FlgH: ") + error.message);
  Check(Global(restarted, "EndClear") != 0 &&
            Global(restarted, "EndNo") == 21 &&
            Global(restarted, "AllClear") != 0 &&
            Global(restarted, "SP100") != 0 &&
            restarted.Persistence().GlobalIntegerIsSet("SP100") &&
            restarted.Persistence().GlobalIntegerIsSet(replayKey),
        "Disk reload lost AllClear, ending, checkpoint or replay flags");
  Check(Kotonoha::SchoolDaysReplayModel::AnyHSceneUnlocked(
            restarted.State(), &unlocked, &error) &&
            unlocked,
        "Replay remained locked after disk GlobalFlag reload");
  result = Resolve(profile, restarted);
  Check(result.kind == Resolver::Kind::AllClear &&
            !result.promotedAllClear &&
            Serialize(restarted) == promoted,
        "Disk reload changed AllClear background or persistence bytes");

  // Malformed input must fail closed and preserve the previous valid session.
  auto truncated = reloaded;
  truncated.pop_back();
  Check(!restarted.LoadGlobalFlag(truncated, &error) &&
            error.code == KOTONOHA_KTRF_ERROR_FORMAT &&
            Serialize(restarted) == promoted,
        "Truncated GlobalFlag replaced a valid profile");
  auto wrongMagic = reloaded;
  wrongMagic[0] = 'X';
  Check(!restarted.LoadGlobalFlag(wrongMagic, &error) &&
            error.code == KOTONOHA_KTRF_ERROR_FORMAT &&
            Serialize(restarted) == promoted &&
            ReadFile(globalPath) == promoted,
        "Bad-magic GlobalFlag corrupted memory or disk");

  Kotonoha::SchoolDaysKtrfScene resetScene;
  Check(restarted.ResetNewGame(&resetScene, &error) &&
            Global(restarted, "AllClear") != 0 &&
            Global(restarted, "SP100") != 0,
        "New game erased globally persisted unlocks");
  TestConfiguredCount(route, assets);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: SchoolDaysAllClearSyntheticTest "
                 "<route.ktnroute> <synthetic-assets-root>\n";
    return 2;
  }
  try {
    TestAllClear(argv[1], argv[2]);
    std::cout << "SchoolDaysAllClearSyntheticTest: PASS (synthetic only)\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "SchoolDaysAllClearSyntheticTest: " << exception.what()
              << '\n';
    return 2;
  }
}
