#include <Kotonoha/routing/SchoolDaysBackRules.hpp>
#include <Kotonoha/routing/SchoolDaysEndBackgroundResolver.hpp>
#include <Kotonoha/routing/SchoolDaysKtrfSession.hpp>
#include <Kotonoha/routing/SchoolDaysReplayModel.hpp>
#include <Kotonoha/routing/SchoolDaysRouteMapOracle.hpp>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
[[noreturn]] void Fail(const std::string &message) {
  std::cerr << "KtrfSchoolDaysSessionTest: " << message << '\n';
  std::exit(2);
}
void Check(bool condition, const std::string &message) {
  if (!condition)
    Fail(message);
}
bool EndsWith(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}
int64_t FirstChoiceValue(const Kotonoha_KtrfAdapter *adapter,
                         Kotonoha_KtrfError *error) {
  uint32_t count = 0;
  if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &count, error))
    Fail(error->message);
  if (count != 1u)
    Fail("expected exactly one current choice");
  uint32_t choiceIndex = KOTONOHA_KTRF_NULL_INDEX;
  if (!Kotonoha_KtrfRouterCurrentChoiceAt(&adapter->router, 0u, &choiceIndex,
                                          error))
    Fail(error->message);
  Kotonoha_KtrfChoice choice{};
  if (!Kotonoha_KtrfGetChoice(adapter->document, choiceIndex, &choice, error))
    Fail(error->message);
  Check(choice.option_count > 0u, "choice has no options");
  Kotonoha_KtrfChoiceOption option{};
  if (!Kotonoha_KtrfGetChoiceOption(adapter->document, choiceIndex, 0u, &option,
                                    error))
    Fail(error->message);
  Check(option.value.kind == KOTONOHA_KTRF_SCALAR_INT64,
        "first choice option is not int64");
  return option.value.as.int64_value;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr
        << "usage: KtrfSchoolDaysSessionTest <route.ktnroute> <assets-root>\n";
    return 2;
  }
  Kotonoha_KtrfError error{};
  {
    Kotonoha::SchoolDaysKtrfSession unlockSource;
    Check(unlockSource.Open(argv[1], argv[2], &error), error.message);
    bool unlocked = true;
    Check(Kotonoha::SchoolDaysReplayModel::AnyHSceneUnlocked(
              unlockSource.State(), &unlocked, &error) && !unlocked,
          "fresh routing globals exposed Replay");
    const char *first = Kotonoha::SchoolDaysReplayModel::HSceneUnlockKey(0);
    const char *second = Kotonoha::SchoolDaysReplayModel::HSceneUnlockKey(40);
    Check(unlockSource.Persistence().WriteGlobalInteger(
              first, 11, -1, &unlockSource.State(), &error), error.message);
    Check(Kotonoha::SchoolDaysReplayModel::AnyHSceneUnlocked(
              unlockSource.State(), &unlocked, &error) && unlocked,
          "first persisted REP flag did not enable Replay");
    Check(unlockSource.Persistence().WriteGlobalInteger(
              second, 11, -1, &unlockSource.State(), &error), error.message);
    std::vector<uint8_t> globalBytes;
    Check(unlockSource.SerializeGlobalFlag(&globalBytes, &error), error.message);
    unlockSource.Close();

    Kotonoha::SchoolDaysKtrfSession restarted;
    Check(restarted.Open(argv[1], argv[2], &error), error.message);
    Check(restarted.LoadGlobalFlag(globalBytes, &error), error.message);
    Check(Kotonoha::SchoolDaysReplayModel::AnyHSceneUnlocked(
              restarted.State(), &unlocked, &error) && unlocked,
          "REP flags did not survive GlobalFlag reload");
    int64_t value = 0;
    Check(restarted.State().ReadGlobalInt(first, &value, &error) && value != 0 &&
              restarted.State().ReadGlobalInt(second, &value, &error) &&
              value != 0 &&
              restarted.State().ReadGlobalInt(
                  Kotonoha::SchoolDaysReplayModel::HSceneUnlockKey(1),
                  &value, &error) && value == 0 &&
              restarted.State().ReadGlobalInt("EndClear", &value, &error) &&
              value == 0,
          "GlobalFlag reload changed unrelated Replay or EndClear flags");
  }
  {
    using Resolver = Kotonoha::SchoolDaysEndBackgroundResolver;
    Kotonoha::SchoolDaysKtrfSession audit;
    Check(audit.Open(argv[1], argv[2], &error), error.message);
    Kotonoha::SchoolDaysKtrfScene firstScene;
    Check(audit.ResetNewGame(&firstScene, &error), error.message);
    auto &state = audit.State();
    auto &persistence = audit.Persistence();
    int64_t value = 1;
    Check(state.ReadGlobalInt("EndClear", &value, &error) && value == 0 &&
              state.ReadGlobalInt("AllClear", &value, &error) && value == 0 &&
              state.ReadGlobalInt("[End00]=\"", &value, &error) && value == 0,
          "fresh profile contained ending or AllClear progress");
    Check(persistence.WriteGlobalInteger("AUDIT_UNRELATED", 3, 17, &state,
                                         &error), error.message);
    Check(persistence.RegisterEnding(0, firstScene.sceneKey, &state, &error) &&
              persistence.RegisterEnding(1, firstScene.sceneKey, &state,
                                         &error) &&
              persistence.RegisterEnding(0, firstScene.sceneKey, &state,
                                         &error), error.message);
    Resolver::Profile profile;
    profile.endBgView = true;
    profile.endingMax = 22;
    profile.baseAsset = "base.png";
    profile.allClearAsset = "all.png";
    for (int i = 0; i < 22; ++i)
      profile.endingAssets.push_back("end" + std::to_string(i) + ".png");
    Resolver::Result result;
    Check(Resolver::Resolve(profile, &state, &result, &error) &&
              result.kind == Resolver::Kind::Ending &&
              result.endingIndex == 0 && !result.promotedAllClear,
          "two endings plus duplicate selected wrong background");
    Check(state.ReadGlobalInt("AllClear", &value, &error) && value == 0,
          "duplicate ending promoted AllClear");
    for (int i = 2; i < 21; ++i)
      Check(persistence.RegisterEnding(i, firstScene.sceneKey, &state, &error),
            error.message);
    Check(Resolver::Resolve(profile, &state, &result, &error) &&
              result.kind == Resolver::Kind::Ending &&
              result.endingIndex == 20 && !result.promotedAllClear,
          "one missing ending promoted AllClear");
    Check(persistence.RegisterEnding(21, firstScene.sceneKey, &state, &error),
          error.message);
    profile.endBgView = false;
    Check(Resolver::Resolve(profile, &state, &result, &error) &&
              result.kind == Resolver::Kind::Base &&
              !result.promotedAllClear &&
              state.ReadGlobalInt("AllClear", &value, &error) && value == 0,
          "EndBGView=0 promoted AllClear");
    profile.endBgView = true;
    profile.allClearAsset.clear();
    Check(!Resolver::Resolve(profile, &state, &result, &error) &&
              state.ReadGlobalInt("AllClear", &value, &error) && value == 0,
          "missing AllClear asset changed state");
    profile.allClearAsset = "all.png";
    Check(Resolver::Resolve(profile, &state, &result, &error) &&
              result.kind == Resolver::Kind::AllClear &&
              result.promotedAllClear && result.asset == "all.png",
          "all 22 endings did not promote AllClear at title resolution");
    Check(persistence.WriteGlobalInteger("AllClear", 11, -1, &state, &error),
          error.message);
    Check(state.ReadGlobalInt("EndClear", &value, &error) && value != 0 &&
              state.ReadGlobalInt("EndNo", &value, &error) && value == 21,
          "AllClear promotion changed EndClear or EndNo");
    std::vector<uint8_t> bytes;
    Check(audit.SerializeGlobalFlag(&bytes, &error), error.message);
    audit.Close();
    Kotonoha::SchoolDaysKtrfSession restarted;
    Check(restarted.Open(argv[1], argv[2], &error) &&
              restarted.LoadGlobalFlag(bytes, &error), error.message);
    Check(restarted.State().ReadGlobalInt("AllClear", &value, &error) &&
              value != 0 &&
              restarted.State().ReadGlobalInt("EndNo", &value, &error) &&
              value == 21 &&
              restarted.State().ReadGlobalInt("AUDIT_UNRELATED", &value,
                                               &error) && value == 17,
          "AllClear, EndNo or unrelated flag did not survive restart");
    Check(restarted.ResetNewGame(&firstScene, &error) &&
              restarted.State().ReadGlobalInt("AllClear", &value, &error) &&
              value != 0,
          "new game erased global AllClear");
  }
  {
    // The original RouteProc GetReadScriptCount enumerates exactly the 1825
    // KTRF SceneKey IDs excluding -OP1, including two routing dispatchers.
    // The two original ORS outside the KTRF graph are not in that native
    // string table. Count global read flags by the authenticated native
    // SceneKey set, not by presence of a physical ORS resource.
    using Oracle = Kotonoha::SchoolDaysRouteMapOracle;
    Kotonoha::SchoolDaysKtrfSession completionAudit;
    Check(completionAudit.Open(argv[1], argv[2], &error), error.message);
    Kotonoha::SchoolDaysKtrfScene firstScene;
    Check(completionAudit.ResetNewGame(&firstScene, &error), error.message);
    auto &flags = completionAudit.State();
    Oracle::Completion initial;
    Check(Oracle::ComputeCompletion(flags,
                                   completionAudit.Adapter()->document,
                                   &initial, &error) &&
              initial.scriptCount == Oracle::kCompletionScriptCount,
          "Route Map did not enumerate the 1825 native SceneKey flags");
    Check(flags.WriteGlobalInt("03/03-B2-A00", 1, &error) &&
              flags.WriteGlobalInt("03/03-KB-E00", 1, &error),
          error.message);
    Oracle::Completion afterDispatcher;
    Check(Oracle::ComputeCompletion(flags,
                                   completionAudit.Adapter()->document,
                                   &afterDispatcher, &error) &&
              afterDispatcher.readCount == initial.readCount + 2,
          "Retail completion no longer counts native dispatcher SceneKeys");
    Check(flags.WriteGlobalInt("01/01-00-OP2", 1, &error) &&
              flags.WriteGlobalInt("05/05-9O-B00", 1, &error),
          error.message);
    Oracle::Completion withRetailOnly;
    Check(Oracle::ComputeCompletion(flags,
                                   completionAudit.Adapter()->document,
                                   &withRetailOnly, &error) &&
              withRetailOnly.readCount == initial.readCount + 2,
          "Retail completion incorrectly counted two unlisted ORS flags");
    Check(Oracle::CompletionPercentForReadCount(200) == 10 &&
              Oracle::CompletionPercentForReadCount(201) == 11 &&
              Oracle::CompletionPercentForReadCount(218) == 11 &&
              Oracle::CompletionPercentForReadCount(219) == 12 &&
              Oracle::CompletionPercentForReadCount(1825) == 100,
          "Native Route Map completion percentage boundaries drifted");
  }
  {
    // SP428 is present at ROUTE15/SCENE85 and ROUTE15/SCENE108 in the native
    // BackRules inventory. The local SLog remains a first-visit collection
    // keyed by SP; revisiting the second coordinate must not manufacture an
    // ambiguous second tag-3 checkpoint for the same selector.
    const char *firstKey =
        Kotonoha::SchoolDaysBackRules::SceneKeyFor(15, 85);
    const char *secondKey =
        Kotonoha::SchoolDaysBackRules::SceneKeyFor(15, 108);
    Check(firstKey != nullptr && secondKey != nullptr &&
              Kotonoha::SchoolDaysBackRules::SpForScene(15, 85) == 428 &&
              Kotonoha::SchoolDaysBackRules::SpForScene(15, 108) == 428,
          "SP428 native two-coordinate source mapping changed");
    for (const bool reverse : {false, true}) {
      Kotonoha::SchoolDaysKtrfSession duplicateSp;
      Check(duplicateSp.Open(argv[1], argv[2], &error), error.message);
      Kotonoha::SchoolDaysKtrfScene firstScene;
      Check(duplicateSp.ResetNewGame(&firstScene, &error), error.message);
      auto &state = duplicateSp.State();
      auto &persistence = duplicateSp.Persistence();
      const int firstCoordinate = reverse ? 108 : 85;
      const int secondCoordinate = reverse ? 85 : 108;
      const char *savedFirstKey = reverse ? secondKey : firstKey;
      const char *savedSecondKey = reverse ? firstKey : secondKey;
      Check(state.WriteCoordinate(15, firstCoordinate, &error) &&
                persistence.RecordCheckpoint(15, firstCoordinate,
                                             savedFirstKey, &state, &error) &&
                state.WriteCoordinate(15, secondCoordinate, &error) &&
                persistence.RecordCheckpoint(15, secondCoordinate,
                                             savedSecondKey, &state, &error),
            error.message);
      size_t sp428Count = 0u;
      for (const auto &entry : persistence.SaveLogDocument().records) {
        if (entry.tag != 3 || entry.secondary != u"SP428")
          continue;
        ++sp428Count;
        Check(entry.name == std::u16string(
                                savedFirstKey,
                                savedFirstKey + std::strlen(savedFirstKey)),
              "SP428 reverse visit changed the first checkpoint SceneKey");
      }
      Check(sp428Count == 1u,
            "SP428 duplicate coordinate generated two tag3 checkpoints");
    }
  }
  {
    Kotonoha::SchoolDaysKtrfSession routeAudit;
    Check(routeAudit.Open(argv[1], argv[2], &error), error.message);
    Kotonoha::SchoolDaysKtrfScene firstScene;
    Check(routeAudit.ResetNewGame(&firstScene, &error), error.message);
    auto &state = routeAudit.State();
    auto &persistence = routeAudit.Persistence();
    Check(persistence.RecordCheckpoint(0, 0, firstScene.sceneKey, &state,
                                       &error), error.message);
    Kotonoha::SchoolDaysKtrfSession::RouteMapCheckpointPlan plan;
    if (!routeAudit.PlanRouteMapCheckpoint(100, &plan, &error))
      Fail(std::string("saved checkpoint could not be selected: ") +
           error.message);
    Check(persistence.GlobalIntegerIsSet("SP100"),
          "checkpoint did not set global SP100");
    Check(persistence.ReplaceCurrentScriptSnapshot(
              firstScene.sceneKey, 0x3f800000u, state, &error), error.message);
    std::vector<uint8_t> savedLog;
    Check(persistence.SerializeSaveLog(&savedLog, &error), error.message);
    persistence.ResetScriptManagerForFrontendReturn();
    Check(!routeAudit.PlanRouteMapCheckpoint(100, &plan, &error) &&
              persistence.GlobalIntegerIsSet("SP100"),
          "global SP flag incorrectly supplied a missing tag3 snapshot");
    Kotonoha::SchoolDaysKtrfSession::LoadPlan loadPlan;
    if (!routeAudit.PlanLoadSave(savedLog, &loadPlan, &error))
      Fail(std::string("saved SLog load plan failed: ") + error.message);
    Check(std::any_of(loadPlan.image.document.records.begin(),
                      loadPlan.image.document.records.end(),
                      [](const auto &record) {
                        return record.tag == 3 && record.secondary == u"SP100";
                      }),
          "serialized SaveFile lost SP100 tag3 checkpoint");
    if (!routeAudit.CommitLoadSave(loadPlan, &error))
      Fail(std::string("saved SLog load commit failed: ") + error.message);
    Check(std::any_of(persistence.SaveLogDocument().records.begin(),
                      persistence.SaveLogDocument().records.end(),
                      [](const auto &record) {
                        return record.tag == 3 && record.secondary == u"SP100";
                      }),
          "SaveFile load discarded SP100 tag3 checkpoint");
    if (!routeAudit.PlanRouteMapCheckpoint(100, &plan, &error))
      Fail(std::string("loaded checkpoint could not be selected: ") +
           error.message);
  }
  {
    Kotonoha::SchoolDaysKtrfSession smallerProfile;
    Check(smallerProfile.Open(argv[1], argv[2], &error), error.message);
    Kotonoha::SchoolDaysKtrfScene firstScene;
    Check(smallerProfile.ResetNewGame(&firstScene, &error), error.message);
    auto &state = smallerProfile.State();
    auto &persistence = smallerProfile.Persistence();
    Check(persistence.RegisterEnding(0, firstScene.sceneKey, &state, &error) &&
              persistence.RegisterEnding(1, firstScene.sceneKey, &state,
                                         &error), error.message);
    Kotonoha::SchoolDaysEndBackgroundResolver::Profile profile;
    profile.endBgView = true;
    profile.endingMax = 2;
    profile.baseAsset = "base.png";
    profile.allClearAsset = "all.png";
    profile.endingAssets = {"end0.png", "end1.png"};
    Kotonoha::SchoolDaysEndBackgroundResolver::Result result;
    Check(Kotonoha::SchoolDaysEndBackgroundResolver::Resolve(
              profile, &state, &result, &error) && result.promotedAllClear,
          "configured EndingMax=2 did not promote after two endings");
  }
  Kotonoha::SchoolDaysKtrfSession session;
  if (!session.Open(argv[1], argv[2], &error))
    Fail(std::string("open failed: ") + error.message);
  Kotonoha::SchoolDaysKtrfScene scene;
  if (!session.ResetNewGame(&scene, &error))
    Fail(std::string("reset failed: ") + error.message);
  Check(scene.sceneKey == "00/00-00-A00", "new-game scene mismatch");
  Check(EndsWith(scene.orsPath, "00-00-A00.ENG.ORS"),
        "new-game ORS path mismatch");

  const auto backCounts = Kotonoha::SchoolDaysBackRules::GetCounts();
  Check(backCounts.knownScenes == 1857u,
        "Back known-scene oracle count mismatch");
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
  const auto inverseA04 =
      Kotonoha::SchoolDaysBackRules::InverseFeelingDelta(0, 4);
  Check(inverseA04[2] == -5, "Route0/Scene4 inverse feeling mismatch");

  auto &state = session.State();
  Check(state.HasRuntimeSession("ROUTE"), "ROUTE is not runtime-backed");
  Check(state.HasRuntimeSession("SCENE"), "SCENE is not runtime-backed");
  Check(!state.HasRuntimeSession("SP_TEST_ONLY"),
        "detached test symbol unexpectedly exists in KTRF");
  int route = -1, sceneIndex = -1;
  if (!state.ReadCoordinate(&route, &sceneIndex, &error))
    Fail(error.message);
  Check(route == 0 && sceneIndex == 0, "new-game semantic coordinate mismatch");

  Kotonoha::KtrfStateStore::Snapshot stateBefore;
  if (!state.Capture(&stateBefore, &error))
    Fail(error.message);

  Kotonoha::SchoolDaysKtrfSession::BackPlan staleBack;
  if (!session.PlanBack(&staleBack, &error))
    Fail(error.message);
  if (!state.WriteCoordinate(0, 1, &error))
    Fail(error.message);
  Check(!session.CommitBack(&staleBack, &error),
        "stale Back plan unexpectedly committed after coordinate change");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  if (!state.WriteSessionInt("SP_TEST_ONLY", 1, &error))
    Fail(error.message);
  int64_t detached = 0;
  if (!state.ReadSessionInt("SP_TEST_ONLY", &detached, &error))
    Fail(error.message);
  Check(detached == 1 && state.DetachedSession().count("SP_TEST_ONLY") == 1u,
        "detached session sidecar mismatch");

  if (!state.WriteSessionInt("001", 0, &error) ||
      !state.AddSessionInt("001", -5, &error))
    Fail(error.message);
  int64_t negativeFeeling = 0;
  if (!state.ReadSessionInt("001", &negativeFeeling, &error))
    Fail(error.message);
  Check(negativeFeeling == -5, "Back-style feeling subtraction was clamped");
  Check(state.DetachedSession().count("001") == 0u,
        "runtime-backed feeling leaked into sidecar");

  Kotonoha::SchoolDaysCoordinate origin;
  origin.route = 0;
  origin.scene = 0;
  origin.nodeIndex = scene.nodeIndex;
  origin.sceneKey = scene.sceneKey;
  if (!session.History().Rebase(origin, state, &error))
    Fail(error.message);
  if (!state.AddSessionInt("002", 7, &error))
    Fail(error.message);
  Kotonoha::SchoolDaysCoordinate destination = origin;
  destination.scene = 1;
  destination.sceneKey = "00/00-00-A01";
  Kotonoha::SchoolDaysHistory::Entry historyEntry;
  if (!session.History().BuildForwardEntry(destination, state, &historyEntry,
                                           &error))
    Fail(error.message);
  Check(historyEntry.inverseFeeling[2] == 7,
        "history did not observe forward feeling delta");
  Check(session.History().CommitForward(historyEntry),
        "history rejected valid forward proposal");
  Kotonoha::SchoolDaysCoordinate predecessor;
  Check(session.History().LatestPredecessor(destination, &predecessor) &&
            predecessor == origin,
        "history predecessor mismatch");

  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();
  Check(state.DetachedSession().empty(),
        "snapshot restore retained detached state");

  auto ApplyBackRule = [&](int sourceRoute, int sourceScene,
                           int destinationRoute, int destinationScene,
                           std::vector<std::string> *hooksOut) {
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
  auto ReadState = [&](const char *symbol) {
    int64_t value = 0;
    if (!state.ReadSessionInt(symbol, &value, &error))
      Fail(error.message);
    return value;
  };

  // Fixed feeling reversal is keyed by the current scene, not by snapshot.
  if (!state.WriteSessionInt("002", 10, &error))
    Fail(error.message);
  std::vector<std::string> backHooks;
  ApplyBackRule(0, 4, 0, 3, &backHooks);
  Check(ReadState("002") == 5, "authenticated inverse feeling was not applied");
  Check(backHooks.size() == 1u && backHooks[0] == "overflow.sdhq:callback_30",
        "feeling 002 Back did not emit authenticated +0x30(1) side effect");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  if (!state.WriteSessionInt("001", 10, &error))
    Fail(error.message);
  backHooks.clear();
  ApplyBackRule(0, 5, 0, 3, &backHooks);
  Check(ReadState("001") == 5 && backHooks.size() == 1u &&
            backHooks[0] == "overflow.sdhq:callback_30",
        "feeling 001 Back did not emit authenticated +0x30(1) side effect");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  if (!state.WriteSessionInt("004", 7, &error))
    Fail(error.message);
  backHooks.clear();
  ApplyBackRule(29, 5, 29, 3, &backHooks);
  Check(ReadState("004") == 6 && backHooks.empty(),
        "feeling 004 incorrectly emitted +0x30 side effect");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  backHooks.clear();
  ApplyBackRule(0, 0, 0, 0, &backHooks);
  Check(backHooks.empty(), "zero/no-effect Back scene emitted callback_30");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  // Unconditional session clear.
  if (!state.WriteSessionInt("986", 1, &error))
    Fail(error.message);
  backHooks.clear();
  ApplyBackRule(1, 58, 1, 40, &backHooks);
  Check(ReadState("986") == 0, "unconditional Back flag clear failed");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  // Conditional clear chain keyed by BS0100N00.
  if (!state.WriteSessionInt("BS0100N00", 0x3f, &error) ||
      !state.WriteSessionInt("997", 1, &error) ||
      !state.WriteSessionInt("995", 1, &error) ||
      !state.WriteSessionInt("994", 1, &error))
    Fail(error.message);
  backHooks.clear();
  ApplyBackRule(1, 73, 1, 63, &backHooks);
  Check(ReadState("997") == 0 && ReadState("995") == 1 && ReadState("994") == 1,
        "conditional Back flag clear chose wrong branch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  // Route 1 scene 51 has the two confirmed direct integer-zero branches.
  if (!state.WriteSessionInt("BS0100I00", 0x25, &error) ||
      !state.WriteSessionInt("999", 9, &error) ||
      !state.WriteSessionInt("998", 8, &error))
    Fail(error.message);
  backHooks.clear();
  ApplyBackRule(1, 51, 1, 37, &backHooks);
  Check(ReadState("999") == 0 && ReadState("998") == 8,
        "Back 999 direct-zero branch mismatch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  if (!state.WriteSessionInt("BS0100I00", 0x26, &error) ||
      !state.WriteSessionInt("999", 9, &error) ||
      !state.WriteSessionInt("998", 8, &error))
    Fail(error.message);
  backHooks.clear();
  ApplyBackRule(1, 51, 1, 38, &backHooks);
  Check(ReadState("998") == 0 && ReadState("999") == 9,
        "Back 998 direct-zero branch mismatch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  // Scene-0 cross-route Back emits callback +0x2C and reverse-mapper SP clear.
  if (!state.WriteSessionInt("SP103", 1, &error))
    Fail(error.message);
  backHooks.clear();
  ApplyBackRule(1, 0, 0, 20, &backHooks);
  Check(ReadState("SP103") == 0,
        "reverse mapper did not clear predecessor SP103");
  Check(backHooks.size() == 1u && backHooks[0] == "overflow.sdhq:callback_2c",
        "scene-0 Back callback +0x2C hook mismatch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  auto ResolveBack = [&](int sourceRoute, int sourceScene) {
    Kotonoha::SchoolDaysBackRules::PredecessorResult result;
    if (!Kotonoha::SchoolDaysBackRules::ResolvePredecessor(
            sourceRoute, sourceScene, &state, &result, &error))
      Fail(std::string("Back predecessor resolve failed: ") + error.message);
    return result;
  };

  auto resolved = ResolveBack(0, 0);
  Check(resolved.kind ==
                Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
            resolved.route == 0 && resolved.scene == 0,
        "fresh new-game Back predecessor mismatch");

  if (!state.WriteSessionInt("BS03BK", 12, &error) ||
      !state.WriteSessionInt("BS03BKA00", 14, &error))
    Fail(error.message);
  resolved = ResolveBack(13, 0);
  Check(resolved.kind ==
                Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
            resolved.route == 15 && resolved.scene == 14,
        "Route13 semantic join predecessor mismatch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  if (!state.WriteSessionInt("BS05KAA05", 4, &error))
    Fail(error.message);
  resolved = ResolveBack(42, 5);
  Check(resolved.kind ==
                Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
            resolved.route == 42 && resolved.scene == 4,
        "Route42 semantic same-route predecessor mismatch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  resolved = ResolveBack(1, 44);
  Check(resolved.kind ==
                Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
            resolved.route == 1 && resolved.scene == 41,
        "Route1 literal asymmetric predecessor mismatch");

  // Two authenticated Back predecessors are routing-only KTRF nodes. They
  // must remain the exact semantic predecessor while being collapsed through
  // native KTRF routing to a physical ORS for presentation.
  if (!session.DebugActivateNode(454u, &error) ||
      !state.WriteCoordinate(12, 1, &error) ||
      !state.WriteSessionInt("001", 0, &error) ||
      !state.WriteSessionInt("002", 1, &error))
    Fail(error.message);
  Kotonoha::SchoolDaysKtrfSession::BackPlan dispatcherBack;
  if (!session.PlanBack(&dispatcherBack, &error))
    Fail(error.message);
  Check(dispatcherBack.kind ==
                Kotonoha::SchoolDaysKtrfSession::BackKind::Scene &&
            dispatcherBack.semanticPredecessor.route == 12 &&
            dispatcherBack.semanticPredecessor.scene == 0 &&
            dispatcherBack.semanticPredecessor.nodeIndex == 453u,
        "Route12 Back did not preserve routing-only semantic predecessor");
  Check(dispatcherBack.destination.route == 18 &&
            dispatcherBack.destination.scene == 60 &&
            dispatcherBack.destination.nodeIndex == 857u &&
            dispatcherBack.scene.sceneKey == "03/03-SB-I03",
        "Route12 routing-only Back did not collapse through second Back "
        "predecessor");
  if (!session.CommitBack(&dispatcherBack, &error))
    Fail(error.message);
  if (!state.ReadCoordinate(&route, &sceneIndex, &error))
    Fail(error.message);
  Check(
      route == 18 && sceneIndex == 60 &&
          session.Adapter()->router.current_node_index == 857u,
      "Route12 routing-only Back commit did not publish physical destination");

  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();
  if (!session.DebugActivateNode(661u, &error) ||
      !state.WriteCoordinate(15, 48, &error) ||
      !state.WriteSessionInt("001", 2, &error) ||
      !state.WriteSessionInt("002", 1, &error) ||
      !state.WriteSessionInt("BS03KBE00", 39, &error) ||
      !state.WriteSessionInt("BS03KBF00", 777, &error))
    Fail(error.message);
  dispatcherBack = {};
  if (!session.PlanBack(&dispatcherBack, &error))
    Fail(error.message);
  Check(dispatcherBack.semanticPredecessor.route == 15 &&
            dispatcherBack.semanticPredecessor.scene == 31 &&
            dispatcherBack.semanticPredecessor.nodeIndex == 644u,
        "Route15 Back did not preserve routing-only semantic predecessor");
  Check(dispatcherBack.destination.route == 15 &&
            dispatcherBack.destination.scene == 39 &&
            dispatcherBack.destination.nodeIndex == 652u &&
            dispatcherBack.scene.sceneKey == "03/03-KB-E08",
        "Route15 routing-only Back did not use BS03KBE00 predecessor");
  if (!session.CommitBack(&dispatcherBack, &error))
    Fail(error.message);
  if (!state.ReadCoordinate(&route, &sceneIndex, &error))
    Fail(error.message);
  Check(
      route == 15 && sceneIndex == 39 &&
          session.Adapter()->router.current_node_index == 652u &&
          ReadState("BS03KBF00") == 777,
      "Route15 routing-only Back commit did not publish physical destination");

  // Flip the forward dispatch condition. Back must still follow BS03KBE00
  // rather than t907/t908, proving the virtual node is traversed in reverse.
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();
  if (!session.DebugActivateNode(661u, &error) ||
      !state.WriteCoordinate(15, 48, &error) ||
      !state.WriteSessionInt("001", 0, &error) ||
      !state.WriteSessionInt("002", 1, &error) ||
      !state.WriteSessionInt("BS03KBE00", 39, &error) ||
      !state.WriteSessionInt("BS03KBF00", 888, &error))
    Fail(error.message);
  dispatcherBack = {};
  if (!session.PlanBack(&dispatcherBack, &error) ||
      dispatcherBack.destination.scene != 39 ||
      !session.CommitBack(&dispatcherBack, &error))
    Fail(error.message);
  Check(ReadState("BS03KBF00") == 888,
        "Route15 collapsed Back depended on forward feeling dispatcher");

  // The virtual R15/S31 Back handler has its own authenticated reverse
  // effects. BS03KBE00=38 must clear 974 while collapsing to physical S38.
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();
  if (!session.DebugActivateNode(661u, &error) ||
      !state.WriteCoordinate(15, 48, &error) ||
      !state.WriteSessionInt("BS03KBE00", 38, &error) ||
      !state.WriteSessionInt("974", 1, &error) ||
      !state.WriteSessionInt("975", 1, &error))
    Fail(error.message);
  dispatcherBack = {};
  if (!session.PlanBack(&dispatcherBack, &error) ||
      dispatcherBack.destination.scene != 38 ||
      !session.CommitBack(&dispatcherBack, &error))
    Fail(error.message);
  Check(ReadState("974") == 0 && ReadState("975") == 1,
        "collapsed R15/S31 Back did not apply its conditional 974 clear");

  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();
  if (!session.DebugActivateNode(661u, &error) ||
      !state.WriteCoordinate(15, 48, &error) ||
      !state.WriteSessionInt("BS03KBE00", 41, &error) ||
      !state.WriteSessionInt("974", 1, &error) ||
      !state.WriteSessionInt("975", 1, &error))
    Fail(error.message);
  dispatcherBack = {};
  if (!session.PlanBack(&dispatcherBack, &error) ||
      dispatcherBack.destination.scene != 41 ||
      !session.CommitBack(&dispatcherBack, &error))
    Fail(error.message);
  Check(ReadState("974") == 1 && ReadState("975") == 0,
        "collapsed R15/S31 Back did not apply its conditional 975 clear");

  // Revalidate the second predecessor at commit time: mutating its BS after
  // planning must reject the stale chain without moving the source scene.
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();
  if (!session.DebugActivateNode(661u, &error) ||
      !state.WriteCoordinate(15, 48, &error) ||
      !state.WriteSessionInt("BS03KBE00", 39, &error))
    Fail(error.message);
  dispatcherBack = {};
  if (!session.PlanBack(&dispatcherBack, &error))
    Fail(error.message);
  if (!state.WriteSessionInt("BS03KBE00", 40, &error))
    Fail(error.message);
  Check(!session.CommitBack(&dispatcherBack, &error),
        "stale collapsed Back plan unexpectedly committed after BS change");
  if (!state.ReadCoordinate(&route, &sceneIndex, &error))
    Fail(error.message);
  Check(route == 15 && sceneIndex == 48 && ReadState("BS03KBE00") == 40,
        "failed stale collapsed Back did not roll back source transaction");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();

  if (!state.WriteSessionInt("BS03SAB07", 1, &error))
    Fail(error.message);
  resolved = ResolveBack(17, 8);
  Check(resolved.kind ==
            Kotonoha::SchoolDaysBackRules::PredecessorKind::NoScript,
        "unhandled Route17 BS domain was guessed instead of no-script result");
  if (!session.DebugActivateNode(732u, &error) ||
      !state.WriteCoordinate(17, 8, &error))
    Fail(error.message);
  Kotonoha::SchoolDaysKtrfSession::BackPlan noScriptBack;
  if (!session.PlanBack(&noScriptBack, &error))
    Fail(error.message);
  Check(noScriptBack.kind ==
            Kotonoha::SchoolDaysKtrfSession::BackKind::NoScript,
        "Route17 unmatched BS did not plan BackNoScript");
  if (!session.CommitBack(&noScriptBack, &error))
    Fail(error.message);
  if (!state.ReadCoordinate(&route, &sceneIndex, &error))
    Fail(error.message);
  Check(route == 17 && sceneIndex == 8 &&
            session.Adapter()->router.current_node_index == 732u,
        "BackNoScript commit changed physical scene/semantic coordinate");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  session.History().Clear();

  resolved = ResolveBack(0, 200);
  Check(resolved.kind ==
                Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
            resolved.route == 0 && resolved.scene == 20,
        "Route0/200 sentinel predecessor mismatch");
  resolved = ResolveBack(0, 220);
  Check(resolved.kind ==
                Kotonoha::SchoolDaysBackRules::PredecessorKind::Resolved &&
            resolved.route == 0 && resolved.scene == 20,
        "Route0/220 sentinel predecessor mismatch");

  resolved = ResolveBack(0, 210);
  Check(resolved.kind ==
            Kotonoha::SchoolDaysBackRules::PredecessorKind::TrialPv,
        "Route0/210 Trial-PV classification mismatch");
  resolved = ResolveBack(0, 999);
  Check(resolved.kind ==
            Kotonoha::SchoolDaysBackRules::PredecessorKind::Boundary,
        "out-of-range Back did not resolve to default boundary");
  resolved = ResolveBack(55, 0);
  Check(resolved.kind ==
            Kotonoha::SchoolDaysBackRules::PredecessorKind::Unresolved,
        "invalid ROUTE incorrectly inherited route-handler boundary semantics");

  backHooks.clear();
  if (!Kotonoha::SchoolDaysBackRules::ApplyBoundary(&state, &backHooks, &error))
    Fail(error.message);
  Check(ReadState("ROUTE") == -1 && backHooks.size() == 1u &&
            backHooks[0] == "overflow.sdhq:callback_2c",
        "Back boundary mode0 effects mismatch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);
  backHooks.clear();
  if (!Kotonoha::SchoolDaysBackRules::ApplyTrialPv(&state, &backHooks, &error))
    Fail(error.message);
  Check(ReadState("SCENE") == 210 && backHooks.size() == 1u &&
            backHooks[0] == "overflow.sdhq:trial_pv_back",
        "Back Trial/PV effects mismatch");
  if (!state.Restore(stateBefore, &error))
    Fail(error.message);

  // A tag4 is historical PlayData state, so Back must keep it.  That means a
  // save taken on a pending revisit cannot use tag4 presence alone to decide
  // whether the active visit already committed its choice.  Reproduce the
  // exact A03 -> choose -> A04 -> Back A03 -> save -> load regression here.
  Kotonoha::SchoolDaysKtrfScene choiceScene;
  if (!session.ResetNewGame(&choiceScene, &error))
    Fail(std::string("choice-save regression reset failed: ") + error.message);
  const char *choicePath[] = {"00/00-00-A01", "00/00-00-A02", "00/00-00-A03"};
  for (const char *expectedScene : choicePath) {
    Kotonoha::SchoolDaysKtrfSession::StepResult step;
    if (!session.Advance("ktrf:next", &step, &error)) {
      Fail(std::string("choice-save setup advance failed: ") + error.message);
    }
    Check(step.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Scene &&
              step.scene.sceneKey == expectedScene,
          std::string("choice-save setup did not reach ") + expectedScene);
    choiceScene = step.scene;
  }
  Check(session.Persistence().ReplaceCurrentScriptSnapshot(
            choiceScene.sceneKey, UINT32_C(0x3f800000), state, &error),
        std::string("A03 tag1 setup failed: ") + error.message);

  // REA RouteProcSDHQ: A04 adds +5 to 002 and its Back handler removes
  // exactly that +5. Re-selecting the same option after Back must never
  // accumulate the discarded branch's feeling. Capture both independent
  // feelings so a spurious 001 mutation cannot hide behind the meter ratio.
  const int64_t beforeA03Choice001 = ReadState("001");
  const int64_t beforeA03Choice002 = ReadState("002");

  const int64_t revisitChoiceValue =
      FirstChoiceValue(session.Adapter(), &error);
  int revisitAccepted = 0;
  if (!session.CommitChoice(revisitChoiceValue, &revisitAccepted, &error)) {
    Fail(std::string("A03 choice commit failed: ") + error.message);
  }
  Check(revisitAccepted != 0, "A03 legal choice was rejected");

  auto HasA03Tag4 = [&]() {
    for (const auto &record : session.Persistence().SaveLogDocument().records) {
      if (record.tag == 4 && record.name == u"00/00-00-A03")
        return true;
    }
    return false;
  };
  auto HasA03Sp101 = [&]() {
    return std::any_of(
        session.Persistence().SaveLogDocument().records.begin(),
        session.Persistence().SaveLogDocument().records.end(),
        [](const auto &record) {
          return record.tag == 3 && record.name == u"00/00-00-A03" &&
                 record.secondary == u"SP101";
        });
  };
  Check(HasA03Tag4(), "A03 choice commit did not create historical tag4");

  auto BuildCurrentSaveBytes = [&](std::vector<uint8_t> *bytes) {
    Kotonoha::SchoolDaysPersistence staged = session.Persistence();
    Kotonoha::KtrfStateStore::Snapshot live;
    if (!state.Capture(&live, &error))
      Fail(error.message);
    const bool committed =
        std::any_of(live.choiceCommitted.begin(), live.choiceCommitted.end(),
                    [](uint8_t value) { return value != 0u; });
    if (!staged.RefreshCurrentScriptSnapshot(state, &error) ||
        !staged.SerializeSaveLogWithLiveChoiceCommitMarker(committed, bytes,
                                                           &error)) {
      Fail(std::string("choice-save serialization failed: ") + error.message);
    }
  };

  std::vector<uint8_t> postChoiceBytes;
  BuildCurrentSaveBytes(&postChoiceBytes);
  Kotonoha::SchoolDaysKtrfSession::LoadPlan postChoiceLoad;
  if (!session.PlanLoadSave(postChoiceBytes, &postChoiceLoad, &error)) {
    Fail(std::string("post-choice save parse failed: ") + error.message);
  }
  Check(postChoiceLoad.recordedChoicePresent &&
            postChoiceLoad.image.liveChoiceCommitMarkerPresent &&
            postChoiceLoad.image.liveChoiceCommitted &&
            postChoiceLoad.restoreRecordedChoice,
        "post-choice save did not preserve current committed-choice semantics");

  Kotonoha::SchoolDaysKtrfSession::StepResult toA04;
  if (!session.Advance("ktrf:next", &toA04, &error))
    Fail(std::string("A03 -> A04 advance failed: ") + error.message);
  Check(toA04.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Scene &&
            toA04.scene.sceneKey == "00/00-00-A04",
        "committed A03 choice did not advance to A04");
  Check(ReadState("001") == beforeA03Choice001 &&
            ReadState("002") == beforeA03Choice002 + 5,
        "first A03->A04 selection did not apply exactly one +5 to 002");
  Check(HasA03Sp101(),
        "A03 first choice exit did not record SP101 for RouteMap");

  Kotonoha::SchoolDaysKtrfSession::BackPlan revisitBack;
  if (!session.PlanBack(&revisitBack, &error) ||
      !session.CommitBack(&revisitBack, &error)) {
    Fail(std::string("A04 -> Back A03 failed: ") + error.message);
  }
  Check(revisitBack.kind == Kotonoha::SchoolDaysKtrfSession::BackKind::Scene &&
            revisitBack.scene.sceneKey == "00/00-00-A03",
        "Back from A04 did not revisit A03");
  Check(ReadState("001") == beforeA03Choice001 &&
            ReadState("002") == beforeA03Choice002,
        "normal Back A04->A03 did not undo the discarded +5 feeling");
  Check(HasA03Sp101(),
        "normal Back incorrectly pruned RouteMap SP101 checkpoint");
  Check(HasA03Tag4(),
        "Back incorrectly deleted historical A03 tag4 needed by PlayData");

  uint32_t revisitChoiceCount = 0u;
  uint32_t revisitChoiceIndex = KOTONOHA_KTRF_NULL_INDEX;
  if (!Kotonoha_KtrfRouterCurrentChoiceCount(&session.Adapter()->router,
                                             &revisitChoiceCount, &error) ||
      revisitChoiceCount != 1u ||
      !Kotonoha_KtrfRouterCurrentChoiceAt(&session.Adapter()->router, 0u,
                                          &revisitChoiceIndex, &error)) {
    Fail(std::string("revisited A03 choice lookup failed: ") + error.message);
  }
  Check(session.Adapter()->router.choice_committed[revisitChoiceIndex] == 0u,
        "Back to A03 did not restore a pending live choice");

  std::vector<uint8_t> pendingRevisitBytes;
  BuildCurrentSaveBytes(&pendingRevisitBytes);
  Kotonoha::SchoolDaysKtrfSession::LoadPlan pendingRevisitLoad;
  if (!session.PlanLoadSave(pendingRevisitBytes, &pendingRevisitLoad, &error)) {
    Fail(std::string("pending-revisit load plan failed: ") + error.message);
  }
  Check(pendingRevisitLoad.recordedChoicePresent &&
            pendingRevisitLoad.image.liveChoiceCommitMarkerPresent &&
            !pendingRevisitLoad.image.liveChoiceCommitted &&
            !pendingRevisitLoad.restoreRecordedChoice,
        "historical A03 tag4 was still mistaken for the current visit");
  Check(std::none_of(pendingRevisitLoad.image.sessionInts.begin(),
                     pendingRevisitLoad.image.sessionInts.end(),
                     [](const auto &item) {
                       return item.first == "__KOTONOHA_LIVE_CHOICE_COMMITTED";
                     }),
        "private live-choice marker leaked into restored tag1 session state");
  if (!session.CommitLoadSave(pendingRevisitLoad, &error)) {
    Fail(std::string("pending-revisit load commit failed: ") + error.message);
  }
  revisitChoiceCount = 0u;
  revisitChoiceIndex = KOTONOHA_KTRF_NULL_INDEX;
  if (!Kotonoha_KtrfRouterCurrentChoiceCount(&session.Adapter()->router,
                                             &revisitChoiceCount, &error) ||
      revisitChoiceCount != 1u ||
      !Kotonoha_KtrfRouterCurrentChoiceAt(&session.Adapter()->router, 0u,
                                          &revisitChoiceIndex, &error)) {
    Fail(std::string("loaded pending A03 choice lookup failed: ") +
         error.message);
  }
  Check(session.Adapter()->router.choice_committed[revisitChoiceIndex] == 0u &&
            HasA03Tag4() &&
            state.DetachedSession().count("__KOTONOHA_LIVE_CHOICE_COMMITTED") ==
                0u,
        "pending A03 LOAD suppressed the choice or lost historical tag4");
  Check(ReadState("001") == beforeA03Choice001 &&
            ReadState("002") == beforeA03Choice002,
        "pending A03 LOAD silently replayed an abandoned choice feeling");

  // Repeat choice -> scene -> Back after the historical tag4 survives and a
  // pending A03 save was restored. This is precisely the sequence reported
  // by the player; simple single-pass Back checks cannot detect the leak.
  for (int repeat = 0; repeat < 2; ++repeat) {
    int acceptedAgain = 0;
    if (!session.CommitChoice(revisitChoiceValue, &acceptedAgain, &error) ||
        acceptedAgain == 0)
      Fail(std::string("A03 re-choice failed: ") + error.message);
    Kotonoha::SchoolDaysKtrfSession::StepResult repeatedForward;
    if (!session.Advance("ktrf:next", &repeatedForward, &error))
      Fail(std::string("A03 repeat advance failed: ") + error.message);
    Check(repeatedForward.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Scene &&
              repeatedForward.scene.sceneKey == "00/00-00-A04" &&
              ReadState("001") == beforeA03Choice001 &&
              ReadState("002") == beforeA03Choice002 + 5,
          "repeating a discarded choice accumulated feel beyond +5");
    Kotonoha::SchoolDaysKtrfSession::BackPlan repeatedBack;
    if (!session.PlanBack(&repeatedBack, &error) ||
        !session.CommitBack(&repeatedBack, &error))
      Fail(std::string("A04 repeat Back failed: ") + error.message);
    Check(repeatedBack.kind == Kotonoha::SchoolDaysKtrfSession::BackKind::Scene &&
              repeatedBack.scene.sceneKey == "00/00-00-A03" &&
              ReadState("001") == beforeA03Choice001 &&
              ReadState("002") == beforeA03Choice002,
          "repeating choice/Back did not restore the original feeling pair");
    Check(HasA03Sp101(),
          "repeated normal Back incorrectly consumed RouteMap SP101");
  }

  // Switching to the other branch after multiple Back cycles must not keep
  // the abandoned +5 for 002. The native Route0 second branch A05 grants
  // +5 to 001, and its own reverse handler removes exactly that value.
  uint32_t alternateChoiceIndex = KOTONOHA_KTRF_NULL_INDEX;
  if (!Kotonoha_KtrfRouterCurrentChoiceAt(
          &session.Adapter()->router, 0u, &alternateChoiceIndex, &error))
    Fail(std::string("A03 alternative choice lookup failed: ") + error.message);
  Kotonoha_KtrfChoiceOption alternateOption{};
  if (!Kotonoha_KtrfGetChoiceOption(session.Adapter()->document,
                                    alternateChoiceIndex, 1u,
                                    &alternateOption, &error) ||
      alternateOption.value.kind != KOTONOHA_KTRF_SCALAR_INT64)
    Fail(std::string("A03 second branch is not a valid integer choice: ") +
         error.message);
  int alternateAccepted = 0;
  if (!session.CommitChoice(alternateOption.value.as.int64_value,
                            &alternateAccepted, &error) ||
      alternateAccepted == 0)
    Fail(std::string("A03 alternate choice failed: ") + error.message);
  Kotonoha::SchoolDaysKtrfSession::StepResult alternateForward;
  if (!session.Advance("ktrf:next", &alternateForward, &error))
    Fail(std::string("A03 -> A05 advance failed: ") + error.message);
  Check(alternateForward.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Scene &&
            alternateForward.scene.sceneKey == "00/00-00-A05" &&
            ReadState("001") == beforeA03Choice001 + 5 &&
            ReadState("002") == beforeA03Choice002,
        "switching A03 branch retained feelings from the abandoned A04 path");
  Kotonoha::SchoolDaysKtrfSession::BackPlan alternateBack;
  if (!session.PlanBack(&alternateBack, &error) ||
      !session.CommitBack(&alternateBack, &error))
    Fail(std::string("A05 -> Back A03 failed: ") + error.message);
  Check(alternateBack.kind == Kotonoha::SchoolDaysKtrfSession::BackKind::Scene &&
            alternateBack.scene.sceneKey == "00/00-00-A03" &&
            ReadState("001") == beforeA03Choice001 &&
            ReadState("002") == beforeA03Choice002 && HasA03Sp101(),
        "Back from alternate A05 branch did not restore both feelings");

  // Save/load during A03's post-choice visual tail remains the opposite case:
  // restore the latch/value only; the choice effects already live in tag1.
  if (!session.PlanLoadSave(postChoiceBytes, &postChoiceLoad, &error) ||
      !session.CommitLoadSave(postChoiceLoad, &error)) {
    Fail(std::string("post-choice save/load restore failed: ") + error.message);
  }
  revisitChoiceCount = 0u;
  revisitChoiceIndex = KOTONOHA_KTRF_NULL_INDEX;
  if (!Kotonoha_KtrfRouterCurrentChoiceCount(&session.Adapter()->router,
                                             &revisitChoiceCount, &error) ||
      revisitChoiceCount != 1u ||
      !Kotonoha_KtrfRouterCurrentChoiceAt(&session.Adapter()->router, 0u,
                                          &revisitChoiceIndex, &error)) {
    Fail(std::string("loaded post-choice A03 lookup failed: ") + error.message);
  }
  const auto &restoredChoice =
      session.Adapter()->router.choice_values[revisitChoiceIndex];
  Check(session.Adapter()->router.choice_committed[revisitChoiceIndex] != 0u &&
            restoredChoice.kind == KOTONOHA_KTRF_RUNTIME_INT64 &&
            restoredChoice.as.int64_value == revisitChoiceValue,
        "post-choice A03 LOAD did not restore committed choice latch/value");

  // Leave the established Gate 3 smoke path on its original clean new-game
  // starting point after the isolated save/load regression above.
  if (!session.ResetNewGame(&scene, &error))
    Fail(std::string("post-regression reset failed: ") + error.message);

  Kotonoha::SchoolDaysKtrfScene current;
  if (!session.CurrentScene(&current, &error))
    Fail(error.message);
  Check(current.sceneKey == scene.sceneKey && current.orsPath == scene.orsPath,
        "CurrentScene mismatch");
  uint32_t loads = 1u, choices = 0u, hooks = 0u, endings = 0u, transitions = 0u;
  uint32_t topMeterRequests = 0u;
  (void)session.ConsumeTopMeterRefreshRequest();
  for (uint32_t step = 0; step < 64u; ++step) {
    const auto *adapter = session.Adapter();
    uint32_t choiceCount = 0u;
    if (!Kotonoha_KtrfRouterCurrentChoiceCount(&adapter->router, &choiceCount,
                                               &error))
      Fail(error.message);
    if (choiceCount != 0u) {
      Kotonoha::SchoolDaysKtrfSession::StepResult blocked;
      if (!session.Advance("ktrf:next", &blocked, &error))
        Fail(error.message);
      Check(blocked.kind ==
                Kotonoha::SchoolDaysKtrfSession::StepKind::BlockedChoice,
            "uncommitted choice did not block");
      int accepted = 0;
      if (!session.CommitChoice(FirstChoiceValue(adapter, &error), &accepted,
                                &error))
        Fail(error.message);
      Check(accepted != 0, "legal choice rejected");
      if (session.ConsumeTopMeterRefreshRequest())
        ++topMeterRequests;
      ++choices;
    }
    Kotonoha::SchoolDaysKtrfSession::StepResult result;
    if (!session.Advance("ktrf:next", &result, &error))
      Fail(error.message);
    if (session.ConsumeTopMeterRefreshRequest())
      ++topMeterRequests;
    hooks += static_cast<uint32_t>(result.hooks.size());
    endings += static_cast<uint32_t>(result.endings.size());
    if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::BlockedChoice)
      Fail("still blocked after choice");
    if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Unresolved)
      Fail("unresolved smoke path");
    if (result.transitionIndex != KOTONOHA_KTRF_NULL_INDEX)
      ++transitions;
    if (result.kind == Kotonoha::SchoolDaysKtrfSession::StepKind::Terminal)
      break;
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
            << "pending_revisit_tag4_load=PASS\n"
            << "post_choice_save_load_latch=PASS\n"
            << "adapter_asset_bridge=PASS\n"
            << "physical_ors_probe_per_scene=PASS\n";
  return 0;
}
