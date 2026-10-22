#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>
#include <Kotonoha/routing/SchoolDaysBackRules.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
[[noreturn]] void Fail(const std::string& message) { std::cerr << "KtrfSchoolDaysSessionTest: " << message << '\n'; std::exit(2); }
void Check(bool condition, const std::string& message) { if (!condition) Fail(message); }
bool EndsWith(const std::string& value, const std::string& suffix) { return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0; }
int64_t FirstChoiceValue(const Kotonoha_SchoolDaysKtrfAdapter* adapter, Kotonoha_KtrfError* error) {
    uint32_t count = 0;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &count, error)) Fail(error->message);
    if (count != 1u) Fail("expected exactly one current choice");
    uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
    if (!Kotonoha_KtrfRouterCurrentChoiceAt(&adapter->router, 0u, &choiceIndex, error)) Fail(error->message);
    Kotonoha_KtrfChoice choice{};
    if (!Kotonoha_KtrfGetChoice(adapter->document, choiceIndex, &choice, error)) Fail(error->message);
    Check(choice.option_count > 0u, "choice has no options");
    Kotonoha_KtrfChoiceOption option{};
    if (!Kotonoha_KtrfGetChoiceOption(adapter->document, choiceIndex, 0u, &option, error)) Fail(error->message);
    Check(option.value.kind == KOTONOHA_KTRF_SCALAR_INT64, "first choice option is not int64");
    return option.value.as.int64_value;
}
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: KtrfSchoolDaysSessionTest <route.ktnroute> <assets-root>\n"; return 2; }
    Kotonoha_KtrfError error{};
    Kotonoha::SchoolDaysKtrfSession session;
    if (!session.Open(argv[1], argv[2], &error)) Fail(std::string("open failed: ") + error.message);
    Kotonoha::SchoolDaysKtrfScene scene;
    if (!session.ResetNewGame(&scene, &error)) Fail(std::string("reset failed: ") + error.message);
    Check(scene.sceneKey == "00/00-00-A00", "new-game scene mismatch");
    Check(EndsWith(scene.orsPath, "00-00-A00.ENG.ORS"), "new-game ORS path mismatch");

    const auto backCounts = Kotonoha::SchoolDaysBackRules::GetCounts();
    Check(backCounts.knownScenes == 1857u, "Back known-scene oracle count mismatch");
    Check(backCounts.inverseFeelingEffects == 283u,
          "Back inverse-feeling oracle count mismatch");
    Check(backCounts.spMappings == 162u, "Back SP oracle count mismatch");
    Check(backCounts.conditionalEvents == 125u,
          "Back expanded conditional-event count mismatch");
    Check(backCounts.predecessorActions == 1880u,
          "Back predecessor-action oracle count mismatch");
    Check(Kotonoha::SchoolDaysBackRules::SpForScene(0, 0) == 100,
          "Route0/Scene0 SP oracle mismatch");
    Check(Kotonoha::SchoolDaysBackRules::SpForScene(0, 1) == -1,
          "Route0/Scene1 unexpectedly has SP mapping");
    const auto inverseA04 = Kotonoha::SchoolDaysBackRules::InverseFeelingDelta(0, 4);
    Check(inverseA04[2] == -5, "Route0/Scene4 inverse feeling mismatch");

    auto& state = session.State();
    Check(state.HasRuntimeSession("ROUTE"), "ROUTE is not runtime-backed");
    Check(state.HasRuntimeSession("SCENE"), "SCENE is not runtime-backed");
    Check(!state.HasRuntimeSession("SP_TEST_ONLY"),
          "detached test symbol unexpectedly exists in KTRF");
    int route = -1, sceneIndex = -1;
    if (!state.ReadCoordinate(&route, &sceneIndex, &error)) Fail(error.message);
    Check(route == 0 && sceneIndex == 0, "new-game semantic coordinate mismatch");

    Kotonoha::SchoolDaysStateStore::Snapshot stateBefore;
    if (!state.Capture(&stateBefore, &error)) Fail(error.message);

    Kotonoha::SchoolDaysKtrfSession::BackPlan staleBack;
    if (!session.PlanBack(&staleBack, &error)) Fail(error.message);
    if (!state.WriteCoordinate(0, 1, &error)) Fail(error.message);
    Check(!session.CommitBack(&staleBack, &error),
          "stale Back plan unexpectedly committed after coordinate change");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    if (!state.WriteSessionInt("SP_TEST_ONLY", 1, &error)) Fail(error.message);
    int64_t detached = 0;
    if (!state.ReadSessionInt("SP_TEST_ONLY", &detached, &error)) Fail(error.message);
    Check(detached == 1 && state.DetachedSession().count("SP_TEST_ONLY") == 1u,
          "detached session sidecar mismatch");

    if (!state.WriteSessionInt("001", 0, &error) ||
        !state.AddSessionInt("001", -5, &error)) Fail(error.message);
    int64_t negativeFeeling = 0;
    if (!state.ReadSessionInt("001", &negativeFeeling, &error)) Fail(error.message);
    Check(negativeFeeling == -5, "Back-style feeling subtraction was clamped");
    Check(state.DetachedSession().count("001") == 0u,
          "runtime-backed feeling leaked into sidecar");

    Kotonoha::SchoolDaysCoordinate origin;
    origin.route = 0;
    origin.scene = 0;
    origin.nodeIndex = scene.nodeIndex;
    origin.sceneKey = scene.sceneKey;
    if (!session.History().Rebase(origin, state, &error)) Fail(error.message);
    if (!state.AddSessionInt("002", 7, &error)) Fail(error.message);
    Kotonoha::SchoolDaysCoordinate destination = origin;
    destination.scene = 1;
    destination.sceneKey = "00/00-00-A01";
    Kotonoha::SchoolDaysHistory::Entry historyEntry;
    if (!session.History().BuildForwardEntry(destination, state, &historyEntry,
                                             &error)) Fail(error.message);
    Check(historyEntry.inverseFeeling[2] == 7,
          "history did not observe forward feeling delta");
    Check(session.History().CommitForward(historyEntry),
          "history rejected valid forward proposal");
    Kotonoha::SchoolDaysCoordinate predecessor;
    Check(session.History().LatestPredecessor(destination, &predecessor) &&
              predecessor == origin,
          "history predecessor mismatch");

    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();
    Check(state.DetachedSession().empty(), "snapshot restore retained detached state");

    auto ApplyBackRule = [&](int sourceRoute, int sourceScene,
                             int destinationRoute, int destinationScene,
                             std::vector<std::string>* hooksOut) {
        Kotonoha::SchoolDaysCoordinate sourceCoord;
        sourceCoord.route = sourceRoute;
        sourceCoord.scene = sourceScene;
        Kotonoha::SchoolDaysCoordinate destinationCoord;
        destinationCoord.route = destinationRoute;
        destinationCoord.scene = destinationScene;
        if (!Kotonoha::SchoolDaysBackRules::Apply(sourceCoord, destinationCoord,
                                                  &state, hooksOut, &error))
            Fail(std::string("Back rule apply failed: ") + error.message);
    };
    auto ReadState = [&](const char* symbol) {
        int64_t value = 0;
        if (!state.ReadSessionInt(symbol, &value, &error)) Fail(error.message);
        return value;
    };

    // Fixed feeling reversal is keyed by the current scene, not by snapshot.
    if (!state.WriteSessionInt("002", 10, &error)) Fail(error.message);
    std::vector<std::string> backHooks;
    ApplyBackRule(0, 4, 0, 3, &backHooks);
    Check(ReadState("002") == 5, "authenticated inverse feeling was not applied");
    Check(backHooks.size() == 1u &&
              backHooks[0] == "overflow.sdhq:callback_30",
          "feeling 002 Back did not emit authenticated +0x30(1) side effect");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    if (!state.WriteSessionInt("001", 10, &error)) Fail(error.message);
    backHooks.clear();
    ApplyBackRule(0, 5, 0, 3, &backHooks);
    Check(ReadState("001") == 5 && backHooks.size() == 1u &&
              backHooks[0] == "overflow.sdhq:callback_30",
          "feeling 001 Back did not emit authenticated +0x30(1) side effect");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    if (!state.WriteSessionInt("004", 7, &error)) Fail(error.message);
    backHooks.clear();
    ApplyBackRule(29, 5, 29, 3, &backHooks);
    Check(ReadState("004") == 6 && backHooks.empty(),
          "feeling 004 incorrectly emitted +0x30 side effect");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    backHooks.clear();
    ApplyBackRule(0, 0, 0, 0, &backHooks);
    Check(backHooks.empty(), "zero/no-effect Back scene emitted callback_30");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    // Unconditional session clear.
    if (!state.WriteSessionInt("986", 1, &error)) Fail(error.message);
    backHooks.clear();
    ApplyBackRule(1, 58, 1, 40, &backHooks);
    Check(ReadState("986") == 0, "unconditional Back flag clear failed");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    // Conditional clear chain keyed by BS0100N00.
    if (!state.WriteSessionInt("BS0100N00", 0x3f, &error) ||
        !state.WriteSessionInt("997", 1, &error) ||
        !state.WriteSessionInt("995", 1, &error) ||
        !state.WriteSessionInt("994", 1, &error)) Fail(error.message);
    backHooks.clear();
    ApplyBackRule(1, 73, 1, 63, &backHooks);
    Check(ReadState("997") == 0 && ReadState("995") == 1 && ReadState("994") == 1,
          "conditional Back flag clear chose wrong branch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    // Route 1 scene 51 has the two confirmed direct integer-zero branches.
    if (!state.WriteSessionInt("BS0100I00", 0x25, &error) ||
        !state.WriteSessionInt("999", 9, &error) ||
        !state.WriteSessionInt("998", 8, &error)) Fail(error.message);
    backHooks.clear();
    ApplyBackRule(1, 51, 1, 37, &backHooks);
    Check(ReadState("999") == 0 && ReadState("998") == 8,
          "Back 999 direct-zero branch mismatch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    if (!state.WriteSessionInt("BS0100I00", 0x26, &error) ||
        !state.WriteSessionInt("999", 9, &error) ||
        !state.WriteSessionInt("998", 8, &error)) Fail(error.message);
    backHooks.clear();
    ApplyBackRule(1, 51, 1, 38, &backHooks);
    Check(ReadState("998") == 0 && ReadState("999") == 9,
          "Back 998 direct-zero branch mismatch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    // Scene-0 cross-route Back emits callback +0x2C and reverse-mapper SP clear.
    if (!state.WriteSessionInt("SP103", 1, &error)) Fail(error.message);
    backHooks.clear();
    ApplyBackRule(1, 0, 0, 20, &backHooks);
    Check(ReadState("SP103") == 0, "reverse mapper did not clear predecessor SP103");
    Check(backHooks.size() == 1u && backHooks[0] == "overflow.sdhq:callback_2c",
          "scene-0 Back callback +0x2C hook mismatch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    auto ResolveBack = [&](int sourceRoute, int sourceScene) {
        Kotonoha::SchoolDaysBackRules::PredecessorResult result;
        if (!Kotonoha::SchoolDaysBackRules::ResolvePredecessor(
                sourceRoute, sourceScene, &state, &result, &error))
            Fail(std::string("Back predecessor resolve failed: ") + error.message);
        return result;
    };

    auto resolved = ResolveBack(0, 0);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
              resolved.route == 0 && resolved.scene == 0,
          "fresh new-game Back predecessor mismatch");

    if (!state.WriteSessionInt("BS03BK", 12, &error) ||
        !state.WriteSessionInt("BS03BKA00", 14, &error)) Fail(error.message);
    resolved = ResolveBack(13, 0);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
              resolved.route == 15 && resolved.scene == 14,
          "Route13 semantic join predecessor mismatch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    if (!state.WriteSessionInt("BS05KAA05", 4, &error)) Fail(error.message);
    resolved = ResolveBack(42, 5);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
              resolved.route == 42 && resolved.scene == 4,
          "Route42 semantic same-route predecessor mismatch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    resolved = ResolveBack(1, 44);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
              resolved.route == 1 && resolved.scene == 41,
          "Route1 literal asymmetric predecessor mismatch");

    // Two authenticated Back predecessors are routing-only KTRF nodes. They
    // must remain the exact semantic predecessor while being collapsed through
    // native KTRF routing to a physical ORS for presentation.
    if (!session.DebugActivateNode(454u, &error) ||
        !state.WriteCoordinate(12, 1, &error) ||
        !state.WriteSessionInt("001", 0, &error) ||
        !state.WriteSessionInt("002", 1, &error)) Fail(error.message);
    Kotonoha::SchoolDaysKtrfSession::BackPlan dispatcherBack;
    if (!session.PlanBack(&dispatcherBack, &error)) Fail(error.message);
    Check(dispatcherBack.kind == Kotonoha::SchoolDaysKtrfSession::BackKind::Scene &&
              dispatcherBack.semanticPredecessor.route == 12 &&
              dispatcherBack.semanticPredecessor.scene == 0 &&
              dispatcherBack.semanticPredecessor.nodeIndex == 453u,
          "Route12 Back did not preserve routing-only semantic predecessor");
    Check(dispatcherBack.destination.route == 18 &&
              dispatcherBack.destination.scene == 60 &&
              dispatcherBack.destination.nodeIndex == 857u &&
              dispatcherBack.scene.sceneKey == "03/03-SB-I03",
          "Route12 routing-only Back did not collapse through second Back predecessor");
    if (!session.CommitBack(&dispatcherBack, &error)) Fail(error.message);
    if (!state.ReadCoordinate(&route, &sceneIndex, &error)) Fail(error.message);
    Check(route == 18 && sceneIndex == 60 &&
              session.Adapter()->router.current_node_index == 857u,
          "Route12 routing-only Back commit did not publish physical destination");

    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();
    if (!session.DebugActivateNode(661u, &error) ||
        !state.WriteCoordinate(15, 48, &error) ||
        !state.WriteSessionInt("001", 2, &error) ||
        !state.WriteSessionInt("002", 1, &error) ||
        !state.WriteSessionInt("BS03KBE00", 39, &error) ||
        !state.WriteSessionInt("BS03KBF00", 777, &error)) Fail(error.message);
    dispatcherBack = {};
    if (!session.PlanBack(&dispatcherBack, &error)) Fail(error.message);
    Check(dispatcherBack.semanticPredecessor.route == 15 &&
              dispatcherBack.semanticPredecessor.scene == 31 &&
              dispatcherBack.semanticPredecessor.nodeIndex == 644u,
          "Route15 Back did not preserve routing-only semantic predecessor");
    Check(dispatcherBack.destination.route == 15 &&
              dispatcherBack.destination.scene == 39 &&
              dispatcherBack.destination.nodeIndex == 652u &&
              dispatcherBack.scene.sceneKey == "03/03-KB-E08",
          "Route15 routing-only Back did not use BS03KBE00 predecessor");
    if (!session.CommitBack(&dispatcherBack, &error)) Fail(error.message);
    if (!state.ReadCoordinate(&route, &sceneIndex, &error)) Fail(error.message);
    Check(route == 15 && sceneIndex == 39 &&
              session.Adapter()->router.current_node_index == 652u &&
              ReadState("BS03KBF00") == 777,
          "Route15 routing-only Back commit did not publish physical destination");

    // Flip the forward dispatch condition. Back must still follow BS03KBE00
    // rather than t907/t908, proving the virtual node is traversed in reverse.
    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();
    if (!session.DebugActivateNode(661u, &error) ||
        !state.WriteCoordinate(15, 48, &error) ||
        !state.WriteSessionInt("001", 0, &error) ||
        !state.WriteSessionInt("002", 1, &error) ||
        !state.WriteSessionInt("BS03KBE00", 39, &error) ||
        !state.WriteSessionInt("BS03KBF00", 888, &error)) Fail(error.message);
    dispatcherBack = {};
    if (!session.PlanBack(&dispatcherBack, &error) ||
        dispatcherBack.destination.scene != 39 ||
        !session.CommitBack(&dispatcherBack, &error)) Fail(error.message);
    Check(ReadState("BS03KBF00") == 888,
          "Route15 collapsed Back depended on forward feeling dispatcher");

    // The virtual R15/S31 Back handler has its own authenticated reverse
    // effects. BS03KBE00=38 must clear 974 while collapsing to physical S38.
    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();
    if (!session.DebugActivateNode(661u, &error) ||
        !state.WriteCoordinate(15, 48, &error) ||
        !state.WriteSessionInt("BS03KBE00", 38, &error) ||
        !state.WriteSessionInt("974", 1, &error) ||
        !state.WriteSessionInt("975", 1, &error)) Fail(error.message);
    dispatcherBack = {};
    if (!session.PlanBack(&dispatcherBack, &error) ||
        dispatcherBack.destination.scene != 38 ||
        !session.CommitBack(&dispatcherBack, &error)) Fail(error.message);
    Check(ReadState("974") == 0 && ReadState("975") == 1,
          "collapsed R15/S31 Back did not apply its conditional 974 clear");

    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();
    if (!session.DebugActivateNode(661u, &error) ||
        !state.WriteCoordinate(15, 48, &error) ||
        !state.WriteSessionInt("BS03KBE00", 41, &error) ||
        !state.WriteSessionInt("974", 1, &error) ||
        !state.WriteSessionInt("975", 1, &error)) Fail(error.message);
    dispatcherBack = {};
    if (!session.PlanBack(&dispatcherBack, &error) ||
        dispatcherBack.destination.scene != 41 ||
        !session.CommitBack(&dispatcherBack, &error)) Fail(error.message);
    Check(ReadState("974") == 1 && ReadState("975") == 0,
          "collapsed R15/S31 Back did not apply its conditional 975 clear");

    // Revalidate the second predecessor at commit time: mutating its BS after
    // planning must reject the stale chain without moving the source scene.
    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();
    if (!session.DebugActivateNode(661u, &error) ||
        !state.WriteCoordinate(15, 48, &error) ||
        !state.WriteSessionInt("BS03KBE00", 39, &error)) Fail(error.message);
    dispatcherBack = {};
    if (!session.PlanBack(&dispatcherBack, &error)) Fail(error.message);
    if (!state.WriteSessionInt("BS03KBE00", 40, &error)) Fail(error.message);
    Check(!session.CommitBack(&dispatcherBack, &error),
          "stale collapsed Back plan unexpectedly committed after BS change");
    if (!state.ReadCoordinate(&route, &sceneIndex, &error)) Fail(error.message);
    Check(route == 15 && sceneIndex == 48 && ReadState("BS03KBE00") == 40,
          "failed stale collapsed Back did not roll back source transaction");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();

    if (!state.WriteSessionInt("BS03SAB07", 1, &error)) Fail(error.message);
    resolved = ResolveBack(17, 8);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::NoScript,
          "unhandled Route17 BS domain was guessed instead of no-script result");
    if (!session.DebugActivateNode(732u, &error) ||
        !state.WriteCoordinate(17, 8, &error)) Fail(error.message);
    Kotonoha::SchoolDaysKtrfSession::BackPlan noScriptBack;
    if (!session.PlanBack(&noScriptBack, &error)) Fail(error.message);
    Check(noScriptBack.kind == Kotonoha::SchoolDaysKtrfSession::BackKind::NoScript,
          "Route17 unmatched BS did not plan BackNoScript");
    if (!session.CommitBack(&noScriptBack, &error)) Fail(error.message);
    if (!state.ReadCoordinate(&route, &sceneIndex, &error)) Fail(error.message);
    Check(route == 17 && sceneIndex == 8 &&
              session.Adapter()->router.current_node_index == 732u,
          "BackNoScript commit changed physical scene/semantic coordinate");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    session.History().Clear();

    resolved = ResolveBack(0, 200);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
              resolved.route == 0 && resolved.scene == 20,
          "Route0/200 sentinel predecessor mismatch");
    resolved = ResolveBack(0, 220);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
              resolved.route == 0 && resolved.scene == 20,
          "Route0/220 sentinel predecessor mismatch");

    resolved = ResolveBack(0, 210);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::TrialPv,
          "Route0/210 Trial-PV classification mismatch");
    resolved = ResolveBack(0, 999);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Boundary,
          "out-of-range Back did not resolve to default boundary");
    resolved = ResolveBack(55, 0);
    Check(resolved.kind == Kotonoha::SchoolDaysBackRules::PredecessorKind::Unresolved,
          "invalid ROUTE incorrectly inherited route-handler boundary semantics");

    backHooks.clear();
    if (!Kotonoha::SchoolDaysBackRules::ApplyBoundary(&state, &backHooks, &error))
        Fail(error.message);
    Check(ReadState("ROUTE") == -1 && backHooks.size() == 1u &&
              backHooks[0] == "overflow.sdhq:callback_2c",
          "Back boundary mode0 effects mismatch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);
    backHooks.clear();
    if (!Kotonoha::SchoolDaysBackRules::ApplyTrialPv(&state, &backHooks, &error))
        Fail(error.message);
    Check(ReadState("SCENE") == 210 && backHooks.size() == 1u &&
              backHooks[0] == "overflow.sdhq:trial_pv_back",
          "Back Trial/PV effects mismatch");
    if (!state.Restore(stateBefore, &error)) Fail(error.message);

    Kotonoha::SchoolDaysKtrfScene current;
    if (!session.CurrentScene(&current, &error)) Fail(error.message);
    Check(current.sceneKey == scene.sceneKey && current.orsPath == scene.orsPath, "CurrentScene mismatch");
    uint32_t loads = 1u, choices = 0u, hooks = 0u, endings = 0u, transitions = 0u;
    uint32_t topMeterRequests = 0u;
    (void)session.ConsumeTopMeterRefreshRequest();
    for (uint32_t step = 0; step < 64u; ++step) {
        const auto* adapter = session.Adapter();
        uint32_t choiceCount = 0u;
        if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &choiceCount, &error)) Fail(error.message);
        if (choiceCount != 0u) {
            Kotonoha::SchoolDaysKtrfSession::StepResult blocked;
            if (!session.Advance("ktrf:next", &blocked, &error)) Fail(error.message);
            Check(blocked.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::BlockedChoice, "uncommitted choice did not block");
            int accepted = 0;
            if (!session.CommitChoice(FirstChoiceValue(adapter, &error), &accepted, &error)) Fail(error.message);
            Check(accepted != 0, "legal choice rejected");
            if (session.ConsumeTopMeterRefreshRequest()) ++topMeterRequests;
            ++choices;
        }
        Kotonoha::SchoolDaysKtrfSession::StepResult result;
        if (!session.Advance("ktrf:next", &result, &error)) Fail(error.message);
        if (session.ConsumeTopMeterRefreshRequest()) ++topMeterRequests;
        hooks += static_cast<uint32_t>(result.hooks.size());
        endings += static_cast<uint32_t>(result.endings.size());
        if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::BlockedChoice) Fail("still blocked after choice");
        if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Unresolved) Fail("unresolved smoke path");
        if (result.transitionIndex != KOTONOHA_KTRF_NULL_INDEX) ++transitions;
        if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Terminal) break;
        Check(!result.scene.sceneKey.empty(), "empty scene-key");
        Check(EndsWith(result.scene.orsPath, ".ENG.ORS"), "wrong ORS extension");
        ++loads;
    }
    Check(transitions >= 8u, "too few transitions");
    Check(loads >= 8u, "too few ORS loads");
    Check(choices >= 1u, "no choice exercised");
    Check(topMeterRequests >= 1u,
          "smoke path produced no authenticated top-meter refresh request");
    std::cout << "SCHOOL DAYS KTRF SESSION GATE 3 PASS\n"
              << "new_game_scene=00/00-00-A00\n"
              << "physical_scene_loads=" << loads << '\n'
              << "transitions=" << transitions << '\n'
              << "choices_committed=" << choices << '\n'
              << "top_meter_refresh_requests=" << topMeterRequests << '\n'
              << "hooks_seen=" << hooks << '\n'
              << "endings_seen=" << endings << '\n'
              << "route_document_ownership=PASS\n"
              << "semantic_state_store=PASS\n"
              << "detached_only_for_missing_symbols=PASS\n"
              << "signed_feeling_no_clamp=PASS\n"
              << "history_predecessor=PASS\n"
              << "stale_back_plan_rejected=PASS\n"
              << "authenticated_back_rule_counts=PASS\n"
              << "authenticated_back_effects=PASS\n"
              << "feeling_callback30_side_effect=PASS\n"
              << "top_meter_refresh_latch=PASS\n"
              << "semantic_predecessor_oracle=PASS\n"
              << "routing_only_back_dispatch=PASS\n"
              << "sentinel_back_predecessors=PASS\n"
              << "back_no_script_commit=PASS\n"
              << "back_boundary_trial_specials=PASS\n"
              << "adapter_asset_bridge=PASS\n"
              << "physical_ors_probe_per_scene=PASS\n";
    return 0;
}
