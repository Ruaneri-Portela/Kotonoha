#include <Kotonoha/routing/SchoolDaysPlayDataSession.hpp>
#include <Kotonoha/routing/SchoolDaysBackRules.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct FakeGameplayStorage { int serial = 0; };

struct Fixture {
    int creates = 0;
    int destroys = 0;
    int live = 0;
    int maxLive = 0;
    int failCreateAt = -1;
};

struct ChoiceSaveFixture {
    std::vector<uint8_t> bytes;
    std::string sceneKey;
    int64_t savedValue = -2;
};

struct AdvanceSaveFixture {
    std::vector<uint8_t> bytes;
    std::string sceneKey;
    std::string nextSceneKey;
};

struct LiveSnapshot {
    std::unordered_map<std::string, int64_t> session;
    std::unordered_map<std::string, int64_t> global;
    std::vector<uint8_t> slog;
    Kotonoha::SchoolDaysCoordinate presented;
    size_t historyEntries = 0u;
    bool hasPresented = false;
};

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SCHOOL DAYS PLAYDATA SESSION GATE FAIL: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) Fail("fixture could not be opened: " + path.string());
    const std::streamoff end = file.tellg();
    if (end <= 0) Fail("fixture is empty: " + path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()))) {
        Fail("fixture read failed: " + path.string());
    }
    return bytes;
}

Kotonoha::Gameplay* Create(const char*, Kotonoha_Game*, void* userdata) {
    auto* fixture = static_cast<Fixture*>(userdata);
    const int call = fixture->creates;
    if (call == fixture->failCreateAt) {
        ++fixture->creates;
        return nullptr;
    }
    auto* storage = new FakeGameplayStorage{};
    storage->serial = ++fixture->creates;
    ++fixture->live;
    fixture->maxLive = std::max(fixture->maxLive, fixture->live);
    return reinterpret_cast<Kotonoha::Gameplay*>(storage);
}

void Destroy(Kotonoha::Gameplay* gameplay, void* userdata) {
    auto* fixture = static_cast<Fixture*>(userdata);
    delete reinterpret_cast<FakeGameplayStorage*>(gameplay);
    ++fixture->destroys;
    --fixture->live;
}

Kotonoha::SchoolDaysKtrfGameplayBridge::Factory FactoryFor(Fixture* fixture) {
    Kotonoha::SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &Create;
    factory.destroy = &Destroy;
    factory.userdata = fixture;
    return factory;
}

bool CoordinateForSceneKey(const std::string& sceneKey, int* route, int* scene) {
    if (route == nullptr || scene == nullptr) return false;
    for (int r = 0; r < 64; ++r) {
        for (int s = 0; s < 256; ++s) {
            const char* key = Kotonoha::SchoolDaysBackRules::SceneKeyFor(r, s);
            if (key != nullptr && sceneKey == key) {
                *route = r;
                *scene = s;
                return true;
            }
        }
    }
    return false;
}

ChoiceSaveFixture BuildChoiceSave(const char* ktrfPath, const char* orsRoot,
                                  Kotonoha_KtrfError* error) {
    Kotonoha::SchoolDaysKtrfSession source;
    if (!source.Open(ktrfPath, orsRoot, error)) Fail(error->message);

    // A03 is the first authenticated physical choice reached by the normal
    // new-game path (A00 -> A01 -> A02 -> A03).  Pin this scene explicitly
    // instead of taking the first entry from the document-wide choice table:
    // choice-table order is not a reachability order and can select a later
    // branch whose path itself crosses an unresolved choice.
    constexpr uint32_t kChoiceNode = 3u;
    constexpr int kRoute = 0;
    constexpr int kScene = 3;
    Kotonoha::SchoolDaysKtrfScene choiceScene;
    if (!source.ResolveSceneNode(kChoiceNode, &choiceScene, error) ||
        choiceScene.sceneKey != "00/00-00-A03" ||
        !source.DebugActivateNode(kChoiceNode, error) ||
        !source.State().WriteCoordinate(kRoute, kScene, error)) {
        Fail(error->message);
    }

    auto* adapter = source.Adapter();
    uint32_t choiceCount = 0u;
    uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
    Kotonoha_KtrfChoiceOption option{};
    if (adapter == nullptr ||
        !Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &choiceCount,
                                               error) ||
        choiceCount == 0u ||
        !Kotonoha_KtrfRouterCurrentChoiceAt(&adapter->router, 0u, &choiceIndex,
                                            error) ||
        !Kotonoha_KtrfGetChoiceOption(adapter->document, choiceIndex, 0u,
                                      &option, error) ||
        option.value.kind != KOTONOHA_KTRF_SCALAR_INT64) {
        source.Close();
        Fail("authenticated A03 choice fixture is unavailable");
    }

    source.Persistence().ResetNewGame();
    int accepted = 0;
    if (!source.CommitChoice(option.value.as.int64_value, &accepted, error) ||
        accepted == 0 ||
        !source.Persistence().ReplaceCurrentScriptSnapshot(
            choiceScene.sceneKey, UINT32_C(0x3f800000), source.State(), error)) {
        Fail(error->message);
    }

    ChoiceSaveFixture fixture;
    fixture.sceneKey = choiceScene.sceneKey;
    fixture.savedValue = option.value.as.int64_value;
    if (!source.Persistence().SerializeSaveLog(&fixture.bytes, error)) {
        Fail(error->message);
    }
    source.Close();
    return fixture;
}

AdvanceSaveFixture BuildAdvanceSave(const char* ktrfPath, const char* orsRoot,
                                    Kotonoha_KtrfError* error) {
    // SaveFile0030.DAT is the authenticated non-terminal control for this gate:
    // ROUTE=0 / SCENE=2 / 00-00-A02, with the frozen oracle's unconditional
    // t2 transition to 00-00-A03. Rebuild only the minimum equivalent SLog from
    // the same route so the test does not depend on an extra proprietary fixture
    // path while still pinning the exact authenticated coordinate.
    constexpr int route = 0;
    constexpr int scene = 2;
    constexpr const char* sceneKey = "00/00-00-A02";
    constexpr const char* nextSceneKey = "00/00-00-A03";

    const char* mapped = Kotonoha::SchoolDaysBackRules::SceneKeyFor(route, scene);
    Check(mapped != nullptr && std::string(mapped) == sceneKey,
          "authenticated advance fixture coordinate drifted");

    Kotonoha::SchoolDaysKtrfSession source;
    if (!source.Open(ktrfPath, orsRoot, error)) Fail(error->message);

    Kotonoha::SchoolDaysKtrfScene resolved;
    if (!source.ResolveSceneKey(sceneKey, &resolved, error) ||
        !source.DebugActivateNode(resolved.nodeIndex, error) ||
        !source.State().WriteCoordinate(route, scene, error)) {
        Fail(error->message);
    }

    source.Persistence().ResetNewGame();
    if (!source.Persistence().ReplaceCurrentScriptSnapshot(
            sceneKey, UINT32_C(0x3f800000), source.State(), error)) {
        Fail(error->message);
    }

    AdvanceSaveFixture fixture;
    fixture.sceneKey = sceneKey;
    fixture.nextSceneKey = nextSceneKey;
    if (!source.Persistence().SerializeSaveLog(&fixture.bytes, error)) {
        Fail(error->message);
    }
    source.Close();
    return fixture;
}

LiveSnapshot CaptureLive(const Kotonoha::SchoolDaysKtrfSession& session,
                         Kotonoha_KtrfError* error) {
    LiveSnapshot snapshot;
    if (!session.State().EnumerateSessionInts(&snapshot.session, error) ||
        !session.State().EnumerateGlobalInts(&snapshot.global, error) ||
        !session.Persistence().SerializeSaveLog(&snapshot.slog, error)) {
        Fail(error->message);
    }
    snapshot.hasPresented = session.History().HasPresented();
    snapshot.presented = session.History().Presented();
    snapshot.historyEntries = session.History().Entries().size();
    return snapshot;
}

bool SameLive(const LiveSnapshot& a, const LiveSnapshot& b) {
    return a.session == b.session && a.global == b.global && a.slog == b.slog &&
           a.hasPresented == b.hasPresented &&
           a.presented == b.presented && a.historyEntries == b.historyEntries;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: SchoolDaysPlayDataSessionTest "
                     "<route.ktnroute> <ors-root> <copied-savefile>\n";
        return 2;
    }

    const auto saveBytes = ReadBytes(argv[3]);
    int gameStub = 0;
    auto* game = reinterpret_cast<Kotonoha_Game*>(&gameStub);
    Kotonoha_KtrfError error{};

    Kotonoha::SchoolDaysKtrfSession expectedSave;
    Check(expectedSave.Open(argv[1], argv[2], &error), error.message);
    Kotonoha::SchoolDaysKtrfSession::LoadPlan expectedPlan;
    Check(expectedSave.PlanLoadSave(saveBytes, &expectedPlan, &error), error.message);
    const std::string expectedSceneKey = expectedPlan.scene.sceneKey;
    expectedSave.Close();

    Fixture fixture;
    Kotonoha::SchoolDaysPlayDataSession session;
    Check(session.Start(argv[1], argv[2], saveBytes, nullptr, game,
                        FactoryFor(&fixture), &error),
          error.message);
    Check(session.IsOpen() && session.CurrentGameplay() != nullptr &&
              session.CurrentScene().sceneKey == expectedSceneKey &&
              !session.CurrentScene().orsPath.empty(),
          "PlayData did not restore the selected SaveFile checkpoint privately");
    Check(session.ChoiceReplayLatchArmed(),
          "PlayData one-shot choice latch was not armed after restore");
    Check(fixture.creates == 1 && fixture.live == 1 && fixture.maxLive == 1,
          "PlayData initial private Gameplay ownership drifted");

    int result = -2;
    bool committed = true;
    Check(session.UpdateChoice(false, &result, &committed, &error) &&
              !committed && result == -2 && session.ChoiceReplayLatchArmed(),
          "SetSELECT-before-start consumed the PlayData choice latch");

    session.Close();
    Check(!session.IsOpen() && fixture.live == 0 && fixture.destroys == 1,
          "PlayData close leaked private Gameplay/session ownership");

    Fixture failedFactory;
    failedFactory.failCreateAt = 0;
    Kotonoha::SchoolDaysPlayDataSession failed;
    Check(!failed.Start(argv[1], argv[2], saveBytes, nullptr, game,
                        FactoryFor(&failedFactory), &error),
          "PlayData accepted a failing private Gameplay factory");
    Check(!failed.IsOpen() && failedFactory.live == 0 &&
              failedFactory.destroys == 0,
          "failed PlayData start retained private ownership");

    // The prompt object exists from ORS construction, but native PlayData does
    // not read tag4 until SetSELECT is actually reached. Build an isolated save
    // at a real physical choice node so this gate exercises the accepted KTRF
    // choice and the one-shot tag4 latch rather than mirroring a boolean helper.
    const ChoiceSaveFixture choiceSave = BuildChoiceSave(argv[1], argv[2], &error);
    Fixture choiceFixture;
    Kotonoha::SchoolDaysPlayDataSession choiceSession;
    Check(choiceSession.Start(argv[1], argv[2], choiceSave.bytes, nullptr, game,
                              FactoryFor(&choiceFixture), &error),
          error.message);
    Check(choiceSession.CurrentScene().sceneKey == choiceSave.sceneKey,
          "choice PlayData fixture did not restore its saved SceneKey");
    const LiveSnapshot oracleBefore = CaptureLive(choiceSession.Session(), &error);
    Check(choiceSession.ReplaceRecordedChoiceOracle(choiceSave.bytes, &error),
          error.message);
    const LiveSnapshot oracleAfter = CaptureLive(choiceSession.Session(), &error);
    Check(SameLive(oracleBefore, oracleAfter) &&
              choiceSession.CurrentScene().sceneKey == choiceSave.sceneKey &&
              choiceSession.ChoiceReplayLatchArmed(),
          "armed PlayData tag4-oracle replacement mutated the private checkpoint");
    result = -2;
    committed = true;
    Check(choiceSession.UpdateChoice(false, &result, &committed, &error) &&
              !committed && result == -2 &&
              choiceSession.ChoiceReplayLatchArmed(),
          "pre-SetSELECT frame consumed saved tag4");
    const LiveSnapshot beforeRecordedSetSelect =
        CaptureLive(choiceSession.Session(), &error);
    Check(choiceSession.UpdateChoice(true, &result, &committed, &error) &&
              committed && result == choiceSave.savedValue &&
              !choiceSession.ChoiceReplayLatchArmed(),
          "reached SetSELECT did not consume exactly the saved tag4 choice");
    const LiveSnapshot afterRecordedSetSelect =
        CaptureLive(choiceSession.Session(), &error);
    Check(SameLive(beforeRecordedSetSelect, afterRecordedSetSelect),
          "restored tag4 SetSELECT replay applied saved choice effects twice");

    choiceSession.Close();
    Check(choiceFixture.live == 0 &&
              choiceFixture.destroys == choiceFixture.creates,
          "choice timing fixture leaked private Gameplay");

    // Keep an independent live session populated while PlayData advances and
    // intentionally fails the *second* physical Gameplay creation. PlayData
    // must discard its private owner atomically and leave the live
    // State/History/Persistence image byte-for-byte/entry-for-entry unchanged.
    Kotonoha::SchoolDaysKtrfSession liveSession;
    Check(liveSession.Open(argv[1], argv[2], &error), error.message);
    Kotonoha::SchoolDaysKtrfScene liveScene;
    Check(liveSession.ResetNewGame(&liveScene, &error), error.message);
    Check(liveSession.State().WriteSessionInt("001", 77, &error), error.message);
    int liveRoute = 0;
    int liveSceneIndex = 0;
    Check(liveSession.State().ReadCoordinate(&liveRoute, &liveSceneIndex, &error),
          error.message);
    Kotonoha::SchoolDaysCoordinate liveCoordinate;
    liveCoordinate.route = liveRoute;
    liveCoordinate.scene = liveSceneIndex;
    liveCoordinate.nodeIndex = liveScene.nodeIndex;
    liveCoordinate.sceneKey = liveScene.sceneKey;
    Check(liveSession.History().Rebase(liveCoordinate, liveSession.State(), &error) &&
              liveSession.Persistence().ReplaceCurrentScriptSnapshot(
                  liveScene.sceneKey, UINT32_C(0x3f800000), liveSession.State(),
                  &error),
          error.message);
    const LiveSnapshot liveBefore = CaptureLive(liveSession, &error);

    const AdvanceSaveFixture advanceSave =
        BuildAdvanceSave(argv[1], argv[2], &error);

    // Prove the isolated fixture still has the authenticated non-terminal
    // A02 -> A03 edge before using it to exercise second-create failure.
    Fixture advanceProbeFixture;
    Kotonoha::SchoolDaysPlayDataSession advanceProbe;
    Check(advanceProbe.Start(argv[1], argv[2], advanceSave.bytes, nullptr, game,
                             FactoryFor(&advanceProbeFixture), &error),
          error.message);
    Check(advanceProbe.CurrentScene().sceneKey == advanceSave.sceneKey,
          "advance fixture did not restore its SaveFile checkpoint");
    bool terminal = true;
    Check(advanceProbe.AdvanceAfterSceneEnd(&terminal, &error) && !terminal &&
              advanceProbe.CurrentScene().sceneKey == advanceSave.nextSceneKey &&
              advanceProbeFixture.creates == 2 &&
              advanceProbeFixture.destroys == 1 && advanceProbeFixture.live == 1,
          "PlayData private saved checkpoint did not advance A02 -> A03");
    advanceProbe.Close();
    Check(advanceProbeFixture.live == 0 && advanceProbeFixture.destroys == 2,
          "advance fixture probe leaked private Gameplay ownership");

    Fixture failAfterAdvance;
    failAfterAdvance.failCreateAt = 1;
    Kotonoha::SchoolDaysPlayDataSession failedAfterAdvance;
    Check(failedAfterAdvance.Start(argv[1], argv[2], advanceSave.bytes, nullptr, game,
                                   FactoryFor(&failAfterAdvance), &error),
          error.message);
    terminal = true;
    Check(!failedAfterAdvance.AdvanceAfterSceneEnd(&terminal, &error),
          "PlayData accepted factory failure after private Advance");
    Check(!failedAfterAdvance.IsOpen() &&
              failedAfterAdvance.CurrentGameplay() == nullptr &&
              failAfterAdvance.creates == 2 && failAfterAdvance.destroys == 1 &&
              failAfterAdvance.live == 0,
          "post-Advance factory failure left mixed private ownership");
    const LiveSnapshot liveAfter = CaptureLive(liveSession, &error);
    Check(SameLive(liveBefore, liveAfter),
          "private PlayData failure mutated live State/History/Persistence");
    liveSession.Close();

    std::vector<uint8_t> malformed{0x53, 0x4c, 0x6f, 0x67};
    Fixture malformedFixture;
    Kotonoha::SchoolDaysPlayDataSession malformedSession;
    Check(!malformedSession.Start(argv[1], argv[2], malformed, nullptr, game,
                                  FactoryFor(&malformedFixture), &error) &&
              malformedFixture.creates == 0 && malformedFixture.live == 0,
          "malformed PlayData SaveFile reached Gameplay creation");

    std::cout << "SCHOOL DAYS PLAYDATA SESSION GATE PASS\n"
              << "copied_save_private_checkpoint_restore=PASS\n"
              << "choice_latch_deferred_until_setselect=PASS\n"
              << "saved_tag4_consumed_at_setselect=PASS\n"
              << "saved_tag4_effects_not_reapplied=PASS\n"
              << "armed_load_tag4_only=PASS\n"
              << "private_gameplay_ownership=PASS\n"
              << "factory_failure_initial_atomic=PASS\n"
              << "factory_failure_after_advance_atomic=PASS\n"
              << "live_state_history_persistence_isolation=PASS\n"
              << "malformed_save_fail_closed=PASS\n";
    return 0;
}
