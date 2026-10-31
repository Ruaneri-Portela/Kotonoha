#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/parsers/KtrfTyped.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

struct Kotonoha_Game {};

namespace Kotonoha {
class TestPresentation final : public SchoolDaysScenePresentation {
public:
    explicit TestPresentation(std::string p) : path(std::move(p)) {}
    std::string path;
    void RestoreChoice(int64_t) override {}
    void SetPaused(bool) override {}
};
}

namespace {

struct FactoryState {
    uint32_t creates = 0;
    uint32_t destroys = 0;
    uint32_t live = 0;
    uint32_t maxLive = 0;
    bool failNextCreate = false;
    std::vector<std::string> createdPaths;
};

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "KtrfSchoolDaysGameplayBridgeTest: " << message << '\n';
    std::exit(2);
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

Kotonoha::Gameplay* FakeCreate(const char* path, Kotonoha_Game*, void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    if (state == nullptr || path == nullptr || *path == '\0') return nullptr;
    if (state->failNextCreate) {
        state->failNextCreate = false;
        return nullptr;
    }
    ++state->creates;
    ++state->live;
    if (state->live > state->maxLive) state->maxLive = state->live;
    state->createdPaths.emplace_back(path);
    return new Kotonoha::TestPresentation(path);
}

void FakeDestroy(Kotonoha::Gameplay* gameplay, void* userdata) {
    auto* state = static_cast<FactoryState*>(userdata);
    if (gameplay == nullptr || state == nullptr) return;
    delete gameplay;
    ++state->destroys;
    if (state->live == 0u) Fail("factory live-count underflow");
    --state->live;
}

int64_t FirstChoiceValue(const Kotonoha_KtrfAdapter* adapter,
                         Kotonoha_KtrfError* error) {
    uint32_t count = 0;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &count, error))
        Fail(std::string("choice count failed: ") + error->message);
    if (count != 1u) Fail("expected exactly one current choice");

    uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
    if (!Kotonoha_KtrfRouterCurrentChoiceAt(
            &adapter->router, 0u, &choiceIndex, error))
        Fail(std::string("choice lookup failed: ") + error->message);

    Kotonoha_KtrfChoice choice{};
    if (!Kotonoha_KtrfGetChoice(adapter->document, choiceIndex, &choice, error))
        Fail(std::string("choice read failed: ") + error->message);
    Check(choice.option_count > 0u, "choice has no options");

    Kotonoha_KtrfChoiceOption option{};
    if (!Kotonoha_KtrfGetChoiceOption(
            adapter->document, choiceIndex, 0u, &option, error))
        Fail(std::string("choice option read failed: ") + error->message);
    Check(option.value.kind == KOTONOHA_KTRF_SCALAR_INT64,
          "first choice option is not int64");
    return option.value.as.int64_value;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool HasCallback2C(const std::vector<std::string>& hooks) {
    for (const auto& hook : hooks) {
        if (hook == "overflow.sdhq:callback_2C" ||
            hook == "overflow.sdhq:callback_2c") return true;
    }
    return false;
}

std::string CurrentScriptSnapshotName(
    const Kotonoha::SchoolDaysPersistence& persistence) {
    const auto& document = persistence.SaveLogDocument();
    for (const auto& record : document.records) {
        if (record.tag != 1) continue;
        std::string name;
        name.reserve(record.name.size());
        for (char16_t unit : record.name) {
            if (unit > 0x7fu) Fail("current tag1 SceneKey is not ASCII");
            name.push_back(static_cast<char>(unit));
        }
        return name;
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: KtrfSchoolDaysGameplayBridgeTest <route.ktnroute> <assets-root>\n";
        return 2;
    }

    FactoryState factoryState;
    Kotonoha::SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &FakeCreate;
    factory.destroy = &FakeDestroy;
    factory.userdata = &factoryState;

    Kotonoha_Game gameContext{};
    Kotonoha_KtrfError error{};
    Kotonoha::SchoolDaysKtrfGameplayBridge bridge;
    if (!bridge.Open(argv[1], argv[2], &gameContext, factory, &error))
        Fail(std::string("open failed: ") + error.message);

    Check(bridge.IsOpen(), "bridge did not enter open state");
    Check(bridge.CurrentGameplay() != nullptr, "initial Gameplay was not created");
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A00",
          "initial scene mismatch");
    Check(EndsWith(bridge.CurrentScene().orsPath, "00-00-A00.ENG.ORS"),
          "initial ORS path mismatch");
    Check(factoryState.creates == 1u && factoryState.destroys == 0u &&
              factoryState.live == 1u,
          "initial Gameplay ownership mismatch");
    Check(bridge.Session().History().HasPresented(),
          "initial physical scene did not rebase semantic history");
    Check(bridge.Session().History().Entries().empty(),
          "new-game history unexpectedly contains transitions");

    // Frontend replay needs arbitrary SceneKey -> physical ORS resolution
    // without activating or otherwise mutating the live narrative session.
    const uint32_t resolveNodeBefore = bridge.Session().Adapter()->router.current_node_index;
    const auto resolveHistoryBefore = bridge.Session().History();
    int resolveRouteBefore = -1, resolveSceneBefore = -1;
    if (!bridge.Session().State().ReadCoordinate(&resolveRouteBefore,
                                                 &resolveSceneBefore, &error)) {
        Fail(error.message);
    }
    Kotonoha::SchoolDaysKtrfScene replayResolved;
    if (!bridge.Session().ResolveSceneKey("03/03-KB-N00", &replayResolved,
                                          &error)) {
        Fail(std::string("read-only replay SceneKey resolve failed: ") +
             error.message);
    }
    Check(replayResolved.sceneKey == "03/03-KB-N00" &&
              EndsWith(replayResolved.orsPath, "03-KB-N00.ENG.ORS"),
          "read-only replay SceneKey resolved the wrong physical ORS");
    int resolveRouteAfter = -1, resolveSceneAfter = -1;
    if (!bridge.Session().State().ReadCoordinate(&resolveRouteAfter,
                                                 &resolveSceneAfter, &error)) {
        Fail(error.message);
    }
    Check(bridge.Session().Adapter()->router.current_node_index == resolveNodeBefore &&
              resolveRouteAfter == resolveRouteBefore &&
              resolveSceneAfter == resolveSceneBefore &&
              bridge.CurrentScene().sceneKey == "00/00-00-A00" &&
              bridge.Session().History().Presented() ==
                  resolveHistoryBefore.Presented() &&
              bridge.Session().History().Entries().size() ==
                  resolveHistoryBefore.Entries().size() &&
              factoryState.creates == 1u && factoryState.destroys == 0u,
          "read-only replay SceneKey resolution mutated the live session");

    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult newGameBack;
    if (!bridge.Back(&newGameBack, &error))
        Fail(std::string("fresh new-game Back failed: ") + error.message);
    Check(newGameBack.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              bridge.CurrentScene().sceneKey == "00/00-00-A00",
          "fresh new-game Back did not resolve Route0/Scene0 to itself");
    Check(factoryState.creates == 2u && factoryState.destroys == 1u &&
              factoryState.live == 1u,
          "fresh new-game Back ownership mismatch");
    Check(bridge.Session().History().Entries().empty(),
          "fresh new-game Back did not rebase history");

    // Prove semantic router state is rolled back if the physical Gameplay
    // construction fails after routing already selected a destination.
    const auto* initialGameplay = bridge.CurrentGameplay();
    const std::string initialSceneKey = bridge.CurrentScene().sceneKey;
    int initialRoute = -1, initialSceneIndex = -1;
    if (!bridge.Session().State().ReadCoordinate(&initialRoute, &initialSceneIndex,
                                                 &error)) Fail(error.message);
    factoryState.failNextCreate = true;
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult failedAdvance;
    Check(!bridge.Advance("ktrf:next", &failedAdvance, &error),
          "forced Gameplay creation failure unexpectedly succeeded");
    int restoredRoute = -1, restoredSceneIndex = -1;
    if (!bridge.Session().State().ReadCoordinate(&restoredRoute,
                                                 &restoredSceneIndex,
                                                 &error)) Fail(error.message);
    Check(restoredRoute == initialRoute && restoredSceneIndex == initialSceneIndex,
          "failed physical swap did not roll back semantic coordinate");
    Check(bridge.CurrentGameplay() == initialGameplay &&
              bridge.CurrentScene().sceneKey == initialSceneKey,
          "failed physical swap changed live Gameplay");
    Check(bridge.Session().History().Entries().empty(),
          "failed physical swap published history");

    // Natural-path Back must first resolve/create the predecessor and only
    // then commit semantic state. Exercise one complete forward/back cycle
    // before the long smoke path.
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult firstForward;
    if (!bridge.Advance("ktrf:next", &firstForward, &error))
        Fail(std::string("first forward after rollback failed: ") + error.message);
    Check(firstForward.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene,
          "first forward after rollback did not swap scene");
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A01",
          "first forward destination mismatch");
    Check(bridge.Session().History().Entries().size() == 1u,
          "first forward did not publish one history entry");

    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult firstBack;
    if (!bridge.Back(&firstBack, &error))
        Fail(std::string("first Back failed: ") + error.message);
    Check(firstBack.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene,
          "first Back did not swap to predecessor");
    Check(bridge.CurrentScene().sceneKey == "00/00-00-A00",
          "first Back predecessor mismatch");
    if (!bridge.Session().State().ReadCoordinate(&restoredRoute,
                                                 &restoredSceneIndex,
                                                 &error)) Fail(error.message);
    Check(restoredRoute == 0 && restoredSceneIndex == 0,
          "first Back semantic coordinate mismatch");
    Check(bridge.Session().History().Entries().empty(),
          "first Back did not pop presented history edge");

    // Title -> INICIAR uses the same authenticated new-game entry but must not
    // behave like deleting GlobalFlag. Prove both transactional factory failure
    // and preservation of runtime-backed + detached global progress.
    Check(bridge.Session().State().WriteGlobalInt("REP02_2K_J03", 1, &error),
          "failed to seed runtime global before new-game reset");
    Check(bridge.Session().State().WriteGlobalInt("EndClear", 1, &error),
          "failed to seed detached global before new-game reset");
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult beforeStart;
    if (!bridge.Advance("ktrf:next", &beforeStart, &error))
        Fail(std::string("pre-start advance failed: ") + error.message);
    Check(beforeStart.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              bridge.CurrentScene().sceneKey == "00/00-00-A01",
          "pre-start setup did not leave initial scene");
    auto* preStartGameplay = bridge.CurrentGameplay();
    factoryState.failNextCreate = true;
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult failedNewGame;
    Check(!bridge.ResetNewGame(&failedNewGame, &error),
          "forced new-game Gameplay creation failure unexpectedly succeeded");
    Check(bridge.CurrentGameplay() == preStartGameplay &&
              bridge.CurrentScene().sceneKey == "00/00-00-A01",
          "failed new-game reset changed the presented scene");
    int64_t globalValue = 0;
    Check(bridge.Session().State().ReadGlobalInt("REP02_2K_J03", &globalValue,
                                                &error) && globalValue == 1,
          "failed new-game reset lost runtime global state");
    Check(bridge.Session().State().ReadGlobalInt("EndClear", &globalValue,
                                                &error) && globalValue == 1,
          "failed new-game reset lost detached global state");

    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult newGame;
    if (!bridge.ResetNewGame(&newGame, &error))
        Fail(std::string("new-game reset failed: ") + error.message);
    Check(newGame.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              bridge.CurrentScene().sceneKey == "00/00-00-A00",
          "new-game reset did not return to authenticated entry scene");
    Check(bridge.Session().History().Entries().empty(),
          "new-game reset retained local semantic history");
    Check(CurrentScriptSnapshotName(bridge.Session().Persistence()) ==
              "00/00-00-A00",
          "new-game reset did not refresh current tag1 snapshot");
    size_t resetTag1 = 0u;
    size_t resetTag3 = 0u;
    size_t resetTag4 = 0u;
    for (const auto& record :
         bridge.Session().Persistence().SaveLogDocument().records) {
        if (record.tag == 1) ++resetTag1;
        if (record.tag == 3) ++resetTag3;
        if (record.tag == 4) ++resetTag4;
    }
    Check(resetTag1 == 1u && resetTag3 == 0u && resetTag4 == 0u,
          "new-game reset retained stale checkpoint/choice persistence");
    Check(bridge.Session().State().ReadGlobalInt("REP02_2K_J03", &globalValue,
                                                &error) && globalValue == 1 &&
              bridge.Session().State().ReadGlobalInt("EndClear", &globalValue,
                                                     &error) && globalValue == 1,
          "successful new-game reset did not preserve GlobalFlag projection");

    // MenuBar -> MainMenu YES tears down only the physical script/gameplay
    // before Title re-entry. The KTRF/global owner must remain live so Title
    // state can be queried and a later INICIAR can create a fresh Gameplay.
    const uint32_t frontendReleaseCreates = factoryState.creates;
    const uint32_t frontendReleaseDestroys = factoryState.destroys;
    Check(bridge.Session().Persistence().HasSaveLog() &&
              !bridge.Session().Persistence().SaveLogDocument().records.empty() &&
              bridge.Session().History().HasPresented(),
          "frontend-release fixture has no live script-manager state");
    Check(bridge.ReleaseCurrentGameplayForFrontend(&error),
          std::string("frontend Gameplay release failed: ") + error.message);
    Check(bridge.IsOpen() && bridge.CurrentGameplay() == nullptr &&
              bridge.CurrentScene().sceneKey.empty() &&
              factoryState.creates == frontendReleaseCreates &&
              factoryState.destroys == frontendReleaseDestroys + 1u &&
              factoryState.live == 0u,
          "frontend release did not destroy exactly the physical Gameplay");
    Check(bridge.Session().Persistence().HasSaveLog() &&
              bridge.Session().Persistence().SaveLogDocument().records.empty() &&
              !bridge.Session().History().HasPresented() &&
              bridge.Session().History().Entries().empty(),
          "frontend release retained stale tag1/tag3/tag4 or semantic History");
    Check(bridge.Session().State().ReadGlobalInt("REP02_2K_J03", &globalValue,
                                                &error) && globalValue == 1 &&
              bridge.Session().State().ReadGlobalInt("EndClear", &globalValue,
                                                     &error) && globalValue == 1,
          "frontend release destroyed live global/session state");

    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult postFrontendStart;
    Check(bridge.ResetNewGame(&postFrontendStart, &error),
          std::string("new game after frontend release failed: ") + error.message);
    Check(postFrontendStart.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              bridge.CurrentGameplay() != nullptr &&
              bridge.CurrentScene().sceneKey == "00/00-00-A00" &&
              factoryState.creates == frontendReleaseCreates + 1u &&
              factoryState.live == 1u,
          "frontend release could not re-enter fresh Gameplay");

    // The debug scene picker must keep the physical NODE and RouteProc source
    // coordinate coherent.  A03's first-choice exit runs callback_00 before it
    // writes the destination coordinate, so leaving a stale SCENE here makes
    // the authenticated SP101 checkpoint impossible and used to terminate the
    // app with "callback_00 source has no authenticated SP mapping".
    if (!bridge.Session().State().WriteCoordinate(0, 2, &error))
        Fail(std::string("debug-choice stale-coordinate setup failed: ") +
             error.message);
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult debugChoiceJump;
    if (!bridge.DebugJumpToNode(3u, &debugChoiceJump, &error))
        Fail(std::string("debug-choice A03 jump failed: ") + error.message);
    int debugChoiceRoute = -1;
    int debugChoiceScene = -1;
    if (!bridge.Session().State().ReadCoordinate(
            &debugChoiceRoute, &debugChoiceScene, &error))
        Fail(std::string("debug-choice coordinate read failed: ") + error.message);
    Check(debugChoiceJump.scene.sceneKey == "00/00-00-A03" &&
              debugChoiceRoute == 0 && debugChoiceScene == 3,
          "debug A03 jump left NODE and ROUTE/SCENE out of sync");

    const int64_t debugChoiceValue =
        FirstChoiceValue(bridge.Session().Adapter(), &error);
    int debugChoiceAccepted = 0;
    if (!bridge.CommitChoice(debugChoiceValue, &debugChoiceAccepted, &error))
        Fail(std::string("debug-choice commit failed: ") + error.message);
    Check(debugChoiceAccepted != 0, "debug-choice legal option was rejected");
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult debugChoiceAdvance;
    if (!bridge.Advance("ktrf:next", &debugChoiceAdvance, &error))
        Fail(std::string("debug-choice advance failed: ") + error.message);
    Check(debugChoiceAdvance.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene,
          "debug-choice A03 exit did not reach a physical destination");
    bool foundSp101 = false;
    for (const auto& record :
         bridge.Session().Persistence().SaveLogDocument().records) {
        if (record.tag == 3 && record.secondary == u"SP101") {
            foundSp101 = true;
            break;
        }
    }
    Check(foundSp101,
          "debug-choice A03 exit did not persist authenticated SP101 checkpoint");

    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult postDebugChoiceReset;
    if (!bridge.ResetNewGame(&postDebugChoiceReset, &error))
        Fail(std::string("reset after debug-choice regression failed: ") +
             error.message);

    // A RouteProc Back predecessor can be a routing-only KTRF node with no
    // ORS. R12/S1 must collapse through virtual R12/S0's own Back handler to
    // one physical R18/S60 swap; the dispatcher must never reach the factory.
    if (!bridge.Session().State().WriteCoordinate(12, 1, &error) ||
        !bridge.Session().State().WriteSessionInt("001", 0, &error) ||
        !bridge.Session().State().WriteSessionInt("002", 1, &error)) {
        Fail(error.message);
    }
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult debugJump;
    if (!bridge.DebugJumpToNode(454u, &debugJump, &error))
        Fail(std::string("Route12 setup jump failed: ") + error.message);
    auto* route12Before = bridge.CurrentGameplay();
    const uint32_t route12Creates = factoryState.creates;
    const uint32_t route12Destroys = factoryState.destroys;
    const size_t route12PathStart = factoryState.createdPaths.size();
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult route12Back;
    if (!bridge.Back(&route12Back, &error))
        Fail(std::string("Route12 collapsed Back failed: ") + error.message);
    Check(route12Back.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              route12Back.scene.sceneKey == "03/03-SB-I03" &&
              bridge.CurrentGameplay() != route12Before,
          "Route12 collapsed Back did not publish the final physical scene");
    Check(factoryState.creates == route12Creates + 1u &&
              factoryState.destroys == route12Destroys + 1u &&
              factoryState.live == 1u,
          "Route12 collapsed Back performed more than one physical swap");
    for (size_t i = route12PathStart; i < factoryState.createdPaths.size(); ++i) {
        Check(factoryState.createdPaths[i].find("03-B2-A00") == std::string::npos,
              "routing-only 03-B2-A00 incorrectly reached Gameplay factory");
    }

    // The same property holds for R15/S48 -> virtual S31 -> BS03KBE00. Force
    // one factory failure first: planning may inspect the chain, but no semantic
    // state/History/Gameplay is allowed to change before the final ORS exists.
    if (!bridge.Session().State().WriteCoordinate(15, 48, &error) ||
        !bridge.Session().State().WriteSessionInt("BS03KBE00", 39, &error) ||
        !bridge.Session().State().WriteSessionInt("BS03KBF00", 777, &error)) {
        Fail(error.message);
    }
    if (!bridge.DebugJumpToNode(661u, &debugJump, &error))
        Fail(std::string("Route15 setup jump failed: ") + error.message);
    auto* route15Before = bridge.CurrentGameplay();
    const auto route15History = bridge.Session().History();
    factoryState.failNextCreate = true;
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult failedBack;
    Check(!bridge.Back(&failedBack, &error),
          "forced collapsed Back Gameplay creation failure unexpectedly succeeded");
    int failedRoute = -1, failedScene = -1;
    if (!bridge.Session().State().ReadCoordinate(&failedRoute, &failedScene, &error))
        Fail(error.message);
    Check(failedRoute == 15 && failedScene == 48 &&
              bridge.CurrentGameplay() == route15Before &&
              bridge.Session().History().Presented() == route15History.Presented() &&
              bridge.Session().History().Entries().size() ==
                  route15History.Entries().size(),
          "failed collapsed Back changed semantic/presentation state");

    const uint32_t route15Creates = factoryState.creates;
    const uint32_t route15Destroys = factoryState.destroys;
    const size_t route15PathStart = factoryState.createdPaths.size();
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult route15Back;
    if (!bridge.Back(&route15Back, &error))
        Fail(std::string("Route15 collapsed Back failed: ") + error.message);
    Check(route15Back.scene.sceneKey == "03/03-KB-E08" &&
              factoryState.creates == route15Creates + 1u &&
              factoryState.destroys == route15Destroys + 1u &&
              factoryState.live == 1u,
          "Route15 collapsed Back did not perform exactly one physical swap");
    int64_t forwardSentinel = 0;
    if (!bridge.Session().State().ReadSessionInt("BS03KBF00", &forwardSentinel,
                                                 &error)) Fail(error.message);
    Check(forwardSentinel == 777,
          "Route15 collapsed Back leaked forward dispatcher effect BS03KBF00");
    for (size_t i = route15PathStart; i < factoryState.createdPaths.size(); ++i) {
        Check(factoryState.createdPaths[i].find("03-KB-E00") == std::string::npos,
              "routing-only 03-KB-E00 incorrectly reached Gameplay factory");
    }

    // Verify the authenticated +0x30(1) feeling-update request survives the
    // full Session -> GameplayBridge Back path, not only direct rule tests.
    if (!bridge.Session().State().WriteCoordinate(0, 4, &error) ||
        !bridge.Session().State().WriteSessionInt("002", 10, &error)) {
        Fail(error.message);
    }
    if (!bridge.DebugJumpToNode(4u, &debugJump, &error))
        Fail(std::string("callback30 setup jump failed: ") + error.message);
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult callback30Back;
    if (!bridge.Back(&callback30Back, &error))
        Fail(std::string("callback30 bridge Back failed: ") + error.message);
    Check(callback30Back.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              callback30Back.scene.sceneKey == "00/00-00-A03",
          "callback30 bridge Back did not reach Route0/Scene3");
    uint32_t callback30Count = 0u;
    for (const auto& hook : callback30Back.hooks) {
        if (hook == "overflow.sdhq:callback_30") ++callback30Count;
    }
    Check(callback30Count == 1u,
          "callback_30 was not preserved exactly once in bridge Back result");

    // Re-open to keep the long forward smoke below rooted at a clean new game.
    bridge.Close();
    if (!bridge.Open(argv[1], argv[2], &gameContext, factory, &error))
        Fail(std::string("re-open after collapsed Back tests failed: ") + error.message);

    // MenuBar SuperSkip must traverse physical scripts through the normal KTRF
    // Advance transaction and stop before an unresolved semantic decision.  It
    // must never manufacture a raw seek across those intervening ORS files.
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult nextChoice;
    if (!bridge.AdvanceToNextChoice("ktrf:next", true, &nextChoice, &error))
        Fail(std::string("next-choice advance failed: ") + error.message);
    Check(nextChoice.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice,
          "clean new-game next-choice advance did not stop at the first choice");
    uint32_t nextChoiceCount = 0u;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(
            &bridge.Session().Adapter()->router, &nextChoiceCount, &error))
        Fail(std::string("next-choice count read failed: ") + error.message);
    Check(nextChoiceCount != 0u && !bridge.HasPendingHandoff(),
          "next-choice stop did not leave the choice-bearing route active");

    // Retail host+0x88 gates cross-script SuperSkip when Config Skip is off:
    // a fresh unread destination must stop at its beginning rather than chain
    // all the way to the first choice. Completing A00 also records A00=true in
    // GlobalFlag, exactly like EXE 0x00424020 -> interface +0x1C(SceneKey,true).
    bridge.Close();
    if (!bridge.Open(argv[1], argv[2], &gameContext, factory, &error))
        Fail(std::string("re-open before unread SuperSkip test failed: ") +
             error.message);
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult unreadStop;
    if (!bridge.AdvanceToNextChoice("ktrf:next", false, &unreadStop, &error))
        Fail(std::string("unread next-choice advance failed: ") + error.message);
    Check(unreadStop.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              unreadStop.scene.sceneKey == "00/00-00-A01",
          "SuperSkip with Skip off did not stop at the first unread script");
    int64_t readMarker = 0;
    Check(bridge.Session().State().ReadGlobalInt("00/00-00-A00", &readMarker,
                                                &error) &&
              readMarker != 0,
          "completed outgoing script was not recorded as globally read");
    Check(bridge.Session().State().ReadGlobalInt("00/00-00-A01", &readMarker,
                                                &error) &&
              readMarker == 0,
          "unread destination was marked read before it completed");

    // Mark A01/A02 as historical reads.  The same Skip-off path may chain
    // through them, but A03 itself is still unread, so native host+0x88 stops
    // at A03's beginning rather than immediately seeking its SetSELECT.
    Check(bridge.Session().Persistence().WriteGlobalInteger(
              "00/00-00-A01", 11, -1, &bridge.Session().State(), &error) &&
              bridge.Session().Persistence().WriteGlobalInteger(
                  "00/00-00-A02", 11, -1, &bridge.Session().State(), &error),
          "failed to seed authenticated read markers for SuperSkip test");
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult unreadChoiceScene;
    if (!bridge.AdvanceToNextChoice("ktrf:next", false, &unreadChoiceScene, &error))
        Fail(std::string("partially-seen next-choice advance failed: ") + error.message);
    Check(unreadChoiceScene.kind ==
              Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene &&
              unreadChoiceScene.scene.sceneKey == "00/00-00-A03",
          "SuperSkip did not stop at the unread choice-bearing script start");

    // Once A03 is globally read, activating SuperSkip while already in A03 is
    // allowed to reach that script's own SkipFRAME == SetSELECT boundary.
    Check(bridge.Session().Persistence().WriteGlobalInteger(
              "00/00-00-A03", 11, -1, &bridge.Session().State(), &error),
          "failed to mark the choice-bearing script as read");
    Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult seenNextChoice;
    if (!bridge.AdvanceToNextChoice("ktrf:next", false, &seenNextChoice, &error))
        Fail(std::string("seen next-choice advance failed: ") + error.message);
    if (seenNextChoice.kind !=
            Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice ||
        bridge.CurrentScene().sceneKey != "00/00-00-A03") {
        Fail("read-marker SuperSkip did not stop at A03's live choice: kind=" +
             std::to_string(static_cast<int>(seenNextChoice.kind)) +
             " scene=" + bridge.CurrentScene().sceneKey);
    }

    // Re-open once more so the established long forward smoke retains its
    // original clean-new-game starting point.
    bridge.Close();
    if (!bridge.Open(argv[1], argv[2], &gameContext, factory, &error))
        Fail(std::string("re-open after next-choice test failed: ") + error.message);

    uint32_t swaps = 0u;
    uint32_t choices = 0u;
    uint32_t blockedChecks = 0u;
    uint32_t pendingHandoffs = 0u;
    uint32_t terminals = 0u;
    uint32_t transitions = 0u;

    for (uint32_t stepIndex = 0u; stepIndex < 64u; ++stepIndex) {
        const auto* adapter = bridge.Session().Adapter();
        uint32_t choiceCount = 0u;
        if (!Kotonoha_KtrfRouterCurrentChoiceCount(
                &adapter->router, &choiceCount, &error))
            Fail(std::string("choice count failed: ") + error.message);

        if (choiceCount != 0u) {
            auto* before = bridge.CurrentGameplay();
            const uint32_t createsBefore = factoryState.creates;
            const uint32_t destroysBefore = factoryState.destroys;

            Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult blocked;
            if (!bridge.Advance("ktrf:next", &blocked, &error))
                Fail(std::string("blocked-choice probe failed: ") + error.message);
            Check(blocked.kind ==
                      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice,
                  "uncommitted choice did not block bridge advance");
            Check(bridge.CurrentGameplay() == before,
                  "blocked choice replaced active Gameplay");
            Check(factoryState.creates == createsBefore &&
                      factoryState.destroys == destroysBefore,
                  "blocked choice touched Gameplay ownership");
            ++blockedChecks;

            const int64_t value = FirstChoiceValue(adapter, &error);
            int accepted = 0;
            if (!bridge.CommitChoice(value, &accepted, &error))
                Fail(std::string("choice commit failed: ") + error.message);
            Check(accepted != 0, "legal choice was rejected");
            ++choices;
        }

        auto* before = bridge.CurrentGameplay();
        const std::string beforeScene = bridge.CurrentScene().sceneKey;
        const uint32_t createsBefore = factoryState.creates;
        const uint32_t destroysBefore = factoryState.destroys;

        Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult result;
        if (!bridge.Advance("ktrf:next", &result, &error))
            Fail(std::string("advance failed: ") + error.message);

        if (result.transitionIndex != KOTONOHA_KTRF_NULL_INDEX) ++transitions;

        switch (result.kind) {
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Unresolved:
            Fail("deterministic bridge smoke path became unresolved");
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BlockedChoice:
            Fail("bridge remained blocked after committing legal choice");
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene:
            Check(bridge.CurrentGameplay() != nullptr &&
                      bridge.CurrentGameplay() != before,
                  "scene advance did not replace Gameplay");
            Check(bridge.CurrentScene().sceneKey == result.scene.sceneKey,
                  "bridge current scene does not match advance result");
            Check(bridge.CurrentScene().sceneKey != beforeScene,
                  "scene advance did not change scene key");
            Check(factoryState.creates == createsBefore + 1u &&
                      factoryState.destroys == destroysBefore + 1u &&
                      factoryState.live == 1u,
                  "scene swap ownership counts are wrong");
            if (HasCallback2C(result.hooks)) {
                Check(bridge.Session().History().Entries().empty(),
                      "callback2C scene swap did not reset transient history");
            }
            else {
                Check(!bridge.Session().History().Entries().empty(),
                      "physical scene swap did not commit history");
            }
            Check(bridge.Session().History().Presented().sceneKey ==
                      bridge.CurrentScene().sceneKey,
                  "history presentation does not match live Gameplay");
            ++swaps;
            break;
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BackBoundary:
            Fail("forward advance unexpectedly returned BackBoundary");
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BackTrialPv:
            Fail("forward advance unexpectedly returned BackTrialPv");
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::BackNoScript:
            Fail("forward advance unexpectedly returned BackNoScript");
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::PendingHandoff: {
            const size_t historyBefore = bridge.Session().History().Entries().size();
            Check(bridge.HasPendingHandoff(), "pending handoff flag not set");
            Check(bridge.CurrentGameplay() == before,
                  "handoff replaced Gameplay before acknowledgement");
            Check(factoryState.creates == createsBefore &&
                      factoryState.destroys == destroysBefore,
                  "pending handoff touched Gameplay ownership");
            Check(bridge.Session().History().Entries().size() == historyBefore,
                  "pending handoff published history before physical swap");
            Check(CurrentScriptSnapshotName(bridge.Session().Persistence()) ==
                      result.scene.sceneKey,
                  "pending handoff did not publish destination tag1 before SaveFeel");
            Check(bridge.CurrentScene().sceneKey == beforeScene,
                  "pending handoff published destination presentation before acknowledgement");
            if (pendingHandoffs == 0u) {
                Check(result.scene.sceneKey == "01/01-00-A00",
                      "first authenticated episode handoff did not target EP2 A00");
            }

            Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult continued;
            if (!bridge.ContinueHandoff(&continued, &error))
                Fail(std::string("continue handoff failed: ") + error.message);
            Check(continued.kind ==
                      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::SwappedScene,
                  "episode handoff did not swap to scene");
            Check(!bridge.HasPendingHandoff(), "handoff flag was not cleared");
            Check(factoryState.creates == createsBefore + 1u &&
                      factoryState.destroys == destroysBefore + 1u &&
                      factoryState.live == 1u,
                  "handoff scene swap ownership counts are wrong");
            Check(HasCallback2C(continued.hooks),
                  "episode handoff did not expose callback2C reset");
            Check(bridge.Session().History().Entries().empty(),
                  "callback2C handoff retained transient history entries");
            Check(bridge.Session().History().Presented().sceneKey ==
                      bridge.CurrentScene().sceneKey,
                  "callback2C handoff history did not rebase to destination");
            ++pendingHandoffs;
            ++swaps;
            break;
        }
        case Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal: {
            Check(bridge.HasPendingHandoff(), "terminal did not enter pending state");
            Check(bridge.CurrentGameplay() == before,
                  "terminal destroyed/replaced active Gameplay");
            Check(HasCallback2C(result.hooks),
                  "terminal RouteProc reset did not expose callback2C after callback38");
            Check(result.scene.sceneKey.empty() && result.scene.orsPath.empty(),
                  "terminal RouteProc reset did not clear outgoing script result");
            int64_t terminalRoute = 0;
            int64_t flagLogo = 1;
            if (!bridge.Session().State().ReadSessionInt("ROUTE", &terminalRoute,
                                                         &error) ||
                !bridge.Session().State().ReadGlobalInt("FLAG_LOGO", &flagLogo,
                                                        &error)) {
                Fail(std::string("terminal reset state read failed: ") + error.message);
            }
            Check(terminalRoute == -1,
                  "terminal RouteProc reset did not write local ROUTE=-1");
            Check(flagLogo == 0,
                  "terminal RouteProc reset did not write global FLAG_LOGO=false");
            Check(bridge.Session().History().Entries().empty(),
                  "terminal callback2C reset retained transient history entries");
            Check(CurrentScriptSnapshotName(bridge.Session().Persistence()) == beforeScene,
                  "terminal reset overwrote the ending tag1 snapshot");

            Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult acknowledged;
            if (!bridge.ContinueHandoff(&acknowledged, &error))
                Fail(std::string("terminal acknowledgement failed: ") + error.message);
            Check(acknowledged.kind ==
                      Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal,
                  "terminal acknowledgement changed kind");
            Check(!bridge.HasPendingHandoff(), "terminal pending flag was not cleared");
            Check(factoryState.creates == createsBefore &&
                      factoryState.destroys == destroysBefore &&
                      factoryState.live == 1u,
                  "terminal acknowledgement touched Gameplay ownership");
            ++terminals;
            stepIndex = 64u;
            break;
        }
        }
    }

    Check(transitions >= 8u, "bridge smoke path executed too few transitions");
    Check(swaps >= 8u, "bridge smoke path performed too few Gameplay swaps");
    Check(choices >= 1u, "bridge smoke path did not commit any choice");
    Check(blockedChecks >= 1u, "bridge smoke path did not prove deferred choice blocking");
    Check(pendingHandoffs >= 1u, "bridge smoke path did not exercise callback_38 handoff");

    // The 64-step smoke above intentionally stays short and need not reach an
    // ending.  Exercise one authenticated terminal directly instead of
    // claiming terminal coverage from a zero-count path.  KTRF node 1306 is
    // 05/05-5H-E00 (ROUTE=32, SCENE=14); its sole transition registers ending
    // 0, invokes callback_38, and terminates.
    if (terminals == 0u) {
        if (!bridge.Session().State().WriteCoordinate(32, 14, &error))
            Fail(std::string("terminal witness coordinate setup failed: ") + error.message);
        Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult terminalJump;
        if (!bridge.DebugJumpToNode(1306u, &terminalJump, &error))
            Fail(std::string("terminal witness debug jump failed: ") + error.message);
        Check(bridge.CurrentScene().sceneKey == "05/05-5H-E00",
              "terminal witness node drifted from authenticated scene");

        auto* terminalGameplay = bridge.CurrentGameplay();
        const std::string terminalScene = bridge.CurrentScene().sceneKey;
        const uint32_t terminalCreates = factoryState.creates;
        const uint32_t terminalDestroys = factoryState.destroys;
        Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult terminalResult;
        if (!bridge.Advance("ktrf:next", &terminalResult, &error))
            Fail(std::string("terminal witness advance failed: ") + error.message);
        Check(terminalResult.kind ==
                  Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal,
              "authenticated terminal witness did not terminate");
        Check(bridge.HasPendingHandoff(),
              "authenticated terminal witness did not enter pending outer flow");
        Check(bridge.CurrentGameplay() == terminalGameplay &&
                  factoryState.creates == terminalCreates &&
                  factoryState.destroys == terminalDestroys,
              "terminal routing reset touched Gameplay ownership before outer acknowledgement");
        Check(HasCallback2C(terminalResult.hooks),
              "terminal RouteProc reset did not expose callback2C after callback38");
        Check(terminalResult.scene.sceneKey.empty() &&
                  terminalResult.scene.orsPath.empty(),
              "terminal RouteProc reset did not clear outgoing script result");

        int64_t terminalRoute = 0;
        int64_t flagLogo = 1;
        if (!bridge.Session().State().ReadSessionInt("ROUTE", &terminalRoute,
                                                     &error) ||
            !bridge.Session().State().ReadGlobalInt("FLAG_LOGO", &flagLogo,
                                                    &error)) {
            Fail(std::string("terminal witness reset state read failed: ") + error.message);
        }
        Check(terminalRoute == -1,
              "terminal witness did not write local ROUTE=-1");
        Check(flagLogo == 0,
              "terminal witness did not write global FLAG_LOGO=false");
        Check(bridge.Session().History().Entries().empty(),
              "terminal witness callback2C retained transient history entries");
        Check(CurrentScriptSnapshotName(bridge.Session().Persistence()) == terminalScene,
              "terminal witness overwrote ending tag1 during RouteProc reset");

        Kotonoha::SchoolDaysKtrfGameplayBridge::StepResult terminalAck;
        if (!bridge.ContinueHandoff(&terminalAck, &error))
            Fail(std::string("terminal witness acknowledgement failed: ") + error.message);
        Check(terminalAck.kind ==
                  Kotonoha::SchoolDaysKtrfGameplayBridge::StepKind::Terminal &&
                  !bridge.HasPendingHandoff(),
              "terminal witness acknowledgement did not close pending outer flow");
        ++terminals;
    }

    Check(terminals >= 1u,
          "Gate 4 did not execute an authenticated terminal routing reset");
    Check(factoryState.live == 1u, "bridge does not own exactly one active Gameplay");
    Check(factoryState.maxLive <= 2u,
          "transactional swap held more than previous+next Gameplay");

    bridge.Close();
    Check(!bridge.IsOpen(), "bridge remained open after Close");
    Check(bridge.CurrentGameplay() == nullptr, "bridge retained Gameplay after Close");
    Check(factoryState.live == 0u, "Gameplay leaked after Close");
    Check(factoryState.creates == factoryState.destroys,
          "Gameplay create/destroy totals differ after Close");

    std::cout << "SCHOOL DAYS KTRF GAMEPLAY BRIDGE GATE 4 PASS\n";
    std::cout << "transitions=" << transitions << '\n';
    std::cout << "gameplay_swaps=" << swaps << '\n';
    std::cout << "choices_committed=" << choices << '\n';
    std::cout << "blocked_choice_checks=" << blockedChecks << '\n';
    std::cout << "episode_handoffs=" << pendingHandoffs << '\n';
    std::cout << "terminals=" << terminals << '\n';
    std::cout << "factory_creates=" << factoryState.creates << '\n';
    std::cout << "factory_destroys=" << factoryState.destroys << '\n';
    std::cout << "max_live_gameplays=" << factoryState.maxLive << '\n';
    std::cout << "single_active_gameplay_ownership=PASS\n";
    std::cout << "deferred_choice_preservation=PASS\n";
    std::cout << "callback38_handoff_preservation=PASS\n";
    std::cout << "handoff_destination_tag1_pre_prompt=PASS\n";
    std::cout << "terminal_routeproc_reset_order=PASS\n";
    std::cout << "semantic_history_two_phase_commit=PASS\n";
    std::cout << "semantic_state_rollback_on_factory_failure=PASS\n";
    std::cout << "routing_only_back_single_swap=PASS\n";
    std::cout << "collapsed_back_factory_failure_atomic=PASS\n";
    std::cout << "callback30_bridge_propagation=PASS\n";
    return 0;
}
