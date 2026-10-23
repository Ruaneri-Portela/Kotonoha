#include <Kotonoha/routing/SchoolDaysKtrfGameplayBridge.hpp>
#include <Kotonoha/routing/SchoolDaysBackRules.hpp>

#include <cstdio>

namespace Kotonoha {

SchoolDaysKtrfGameplayBridge::~SchoolDaysKtrfGameplayBridge() {
    Close();
}

void SchoolDaysKtrfGameplayBridge::SetError(Kotonoha_KtrfError* error,
                                             Kotonoha_KtrfErrorCode code,
                                             const char* message) {
    if (error == nullptr) return;
    error->code = code;
    std::snprintf(error->message, sizeof(error->message), "%s",
                  message != nullptr ? message : "School Days gameplay bridge error");
}

bool SchoolDaysKtrfGameplayBridge::IsCallback38(
    const std::vector<std::string>& hooks) {
    for (const auto& hook : hooks) {
        if (hook == "overflow.sdhq:callback_38") return true;
    }
    return false;
}

bool SchoolDaysKtrfGameplayBridge::IsCallback2C(
    const std::vector<std::string>& hooks) {
    for (const auto& hook : hooks) {
        if (hook == "overflow.sdhq:callback_2C" ||
            hook == "overflow.sdhq:callback_2c") {
            return true;
        }
    }
    return false;
}

bool SchoolDaysKtrfGameplayBridge::CreateAndSwap(
    const SchoolDaysKtrfScene& scene, Kotonoha_KtrfError* error) {
    if (factory.create == nullptr || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay factory is incomplete");
        return false;
    }
    if (scene.orsPath.empty()) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay scene has no ORS path");
        return false;
    }

    Gameplay* next = factory.create(scene.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days gameplay factory failed to create scene");
        return false;
    }

    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = scene;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);
    return true;
}

bool SchoolDaysKtrfGameplayBridge::BuildCoordinate(
    const SchoolDaysKtrfScene& scene, SchoolDaysCoordinate* coordinate,
    Kotonoha_KtrfError* error) const {
    if (coordinate == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days coordinate output is null");
        return false;
    }
    int route = 0;
    int sceneIndex = 0;
    if (!session.State().ReadCoordinate(&route, &sceneIndex, error)) return false;
    coordinate->route = route;
    coordinate->scene = sceneIndex;
    coordinate->nodeIndex = scene.nodeIndex;
    coordinate->sceneKey = scene.sceneKey;
    return true;
}

bool SchoolDaysKtrfGameplayBridge::Open(
    const char* ktrfPath, const char* assetsRoot, Kotonoha_Game* context,
    const Factory& requestedFactory, Kotonoha_KtrfError* error) {
    Close();

    if (context == nullptr || requestedFactory.create == nullptr ||
        requestedFactory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay bridge context/factory is invalid");
        return false;
    }

    gameContext = context;
    factory = requestedFactory;

    if (!session.Open(ktrfPath, assetsRoot, error)) {
        gameContext = nullptr;
        factory = {};
        return false;
    }

    SchoolDaysKtrfScene first;
    if (!session.ResetNewGame(&first, error) || !CreateAndSwap(first, error)) {
        Close();
        return false;
    }
    SchoolDaysCoordinate firstCoordinate;
    if (!BuildCoordinate(first, &firstCoordinate, error) ||
        !session.History().Rebase(firstCoordinate, session.State(), error) ||
        !session.Persistence().ReplaceCurrentScriptSnapshot(
            first.sceneKey, UINT32_C(0x3f800000), session.State(), error)) {
        Close();
        return false;
    }

    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    opened = true;
    return true;
}

void SchoolDaysKtrfGameplayBridge::Close() {
    if (currentGameplay != nullptr && factory.destroy != nullptr) {
        factory.destroy(currentGameplay, factory.userdata);
    }
    currentGameplay = nullptr;
    currentScene = {};
    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    session.Close();
    factory = {};
    gameContext = nullptr;
    opened = false;
}

bool SchoolDaysKtrfGameplayBridge::CommitChoice(
    int64_t value, int* accepted, Kotonoha_KtrfError* error) {
    if (!opened || pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay bridge cannot commit choice now");
        return false;
    }
    return session.CommitChoice(value, accepted, error);
}

bool SchoolDaysKtrfGameplayBridge::Advance(
    const char* trigger, StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay bridge is not open or result is null");
        return false;
    }
    if (pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay bridge has a pending handoff");
        return false;
    }

    *out = {};
    SchoolDaysStateStore::Snapshot before;
    if (!session.State().Capture(&before, error)) return false;
    const SchoolDaysPersistence persistenceBefore = session.Persistence();
    SchoolDaysKtrfSession::StepResult step;
    if (!session.Advance(trigger, &step, error)) {
        Kotonoha_KtrfError restoreError{};
        (void)session.State().Restore(before, &restoreError);
        session.Persistence() = persistenceBefore;
        return false;
    }

    out->transitionIndex = step.transitionIndex;
    out->hopCount = step.hopCount;
    out->scene = step.scene;
    out->hooks = step.hooks;
    out->endings = step.endings;

    switch (step.kind) {
    case SchoolDaysKtrfSession::StepKind::Unresolved:
        out->kind = StepKind::Unresolved;
        return true;
    case SchoolDaysKtrfSession::StepKind::BlockedChoice:
        out->kind = StepKind::BlockedChoice;
        return true;
    case SchoolDaysKtrfSession::StepKind::Terminal:
        if (!session.CommitTerminalRoutingReset(&step, error)) {
            Kotonoha_KtrfError restoreError{};
            (void)session.State().Restore(before, &restoreError);
            session.Persistence() = persistenceBefore;
            return false;
        }
        out->hooks = step.hooks;
        out->scene = step.scene;
        out->kind = StepKind::Terminal;
        pendingStep = step;
        pendingHandoff = true;
        return true;
    case SchoolDaysKtrfSession::StepKind::Scene:
        {
        SchoolDaysCoordinate destination;
        SchoolDaysHistory::Entry historyEntry;
        if (!BuildCoordinate(step.scene, &destination, error) ||
            !session.History().BuildForwardEntry(destination, session.State(),
                                                 &historyEntry, error)) {
            Kotonoha_KtrfError restoreError{};
            (void)session.State().Restore(before, &restoreError);
            session.Persistence() = persistenceBefore;
            return false;
        }
        if (IsCallback38(step.hooks)) {
            // The native outer flow has already copied the destination
            // SceneKey/FlgH into the current tag-1 snapshot before it raises
            // callback_38 and opens SaveFeel.  A save made from that prompt
            // must therefore describe the destination even though presentation
            // and History still belong to the outgoing Gameplay.
            if (!session.Persistence().ReplaceCurrentScriptSnapshot(
                    step.scene.sceneKey, UINT32_C(0x3f800000),
                    session.State(), error)) {
                Kotonoha_KtrfError restoreError{};
                (void)session.State().Restore(before, &restoreError);
                session.Persistence() = persistenceBefore;
                return false;
            }
            out->kind = StepKind::PendingHandoff;
            pendingStep = step;
            pendingHistoryEntry = historyEntry;
            pendingHistoryEntryValid = true;
            pendingHandoff = true;
            return true;
        }
        Gameplay* next = factory.create(step.scene.orsPath.c_str(), gameContext,
                                        factory.userdata);
        if (next == nullptr) {
            Kotonoha_KtrfError restoreError{};
            (void)session.State().Restore(before, &restoreError);
            session.Persistence() = persistenceBefore;
            SetError(error, KOTONOHA_KTRF_ERROR_IO,
                     "School Days gameplay factory failed to create scene");
            return false;
        }
        const SchoolDaysHistory historyBefore = session.History();
        const bool historyCommitted = IsCallback2C(step.hooks)
            ? session.History().Rebase(historyEntry.destination, session.State(), error)
            : session.History().CommitForward(historyEntry);
        if (!historyCommitted) {
            Kotonoha_KtrfError restoreError{};
            (void)session.State().Restore(before, &restoreError);
            session.Persistence() = persistenceBefore;
            session.History() = historyBefore;
            factory.destroy(next, factory.userdata);
            SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                     "School Days history update failed during scene swap");
            return false;
        }
        if (!session.Persistence().ReplaceCurrentScriptSnapshot(
                step.scene.sceneKey, UINT32_C(0x3f800000),
                session.State(), error)) {
            Kotonoha_KtrfError restoreError{};
            (void)session.State().Restore(before, &restoreError);
            session.Persistence() = persistenceBefore;
            session.History() = historyBefore;
            factory.destroy(next, factory.userdata);
            return false;
        }
        Gameplay* previous = currentGameplay;
        currentGameplay = next;
        currentScene = step.scene;
        if (previous != nullptr) factory.destroy(previous, factory.userdata);
        out->kind = StepKind::SwappedScene;
        return true;
        }
    }

    SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
             "School Days gameplay bridge received unknown session step");
    return false;
}

bool SchoolDaysKtrfGameplayBridge::AdvanceToNextChoice(
    const char* trigger, StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days next-choice advance requires an open bridge/result");
        return false;
    }

    // SuperSkip crosses physical scripts through the same authenticated KTRF
    // advance transaction used by natural scene completion.  A pending choice,
    // handoff, terminal or unresolved route is a semantic stop; only a completed
    // physical scene swap is eligible to continue immediately.
    for (;;) {
        StepResult step;
        if (!Advance(trigger, &step, error)) return false;
        *out = step;
        if (step.kind != StepKind::SwappedScene) return true;
    }
}

bool SchoolDaysKtrfGameplayBridge::Back(StepResult* out,
                                        Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay Back requires an open bridge/result");
        return false;
    }
    if (pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay Back cannot run during pending handoff");
        return false;
    }

    *out = {};
    SchoolDaysKtrfSession::BackPlan plan;
    if (!session.PlanBack(&plan, error)) return false;
    out->hooks = plan.hooks;

    if (plan.kind == SchoolDaysKtrfSession::BackKind::Unresolved) {
        out->kind = StepKind::Unresolved;
        return true;
    }
    if (plan.kind == SchoolDaysKtrfSession::BackKind::Boundary) {
        if (!session.CommitBack(&plan, error)) return false;
        out->kind = StepKind::BackBoundary;
        out->hooks = plan.hooks;
        return true;
    }
    if (plan.kind == SchoolDaysKtrfSession::BackKind::TrialPv) {
        if (!session.CommitBack(&plan, error)) return false;
        out->kind = StepKind::BackTrialPv;
        out->hooks = plan.hooks;
        return true;
    }
    if (plan.kind == SchoolDaysKtrfSession::BackKind::NoScript) {
        if (!session.CommitBack(&plan, error)) return false;
        out->kind = StepKind::BackNoScript;
        out->hooks = plan.hooks;
        return true;
    }
    if (plan.kind != SchoolDaysKtrfSession::BackKind::Scene) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay Back received unknown plan kind");
        return false;
    }

    if (factory.create == nullptr || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay Back factory is incomplete");
        return false;
    }
    Gameplay* next = factory.create(plan.scene.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days gameplay Back failed to create predecessor scene");
        return false;
    }

    if (!session.CommitBack(&plan, error)) {
        factory.destroy(next, factory.userdata);
        return false;
    }

    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = plan.scene;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);

    out->kind = StepKind::SwappedScene;
    out->scene = plan.scene;
    out->hooks = plan.hooks;
    return true;
}

bool SchoolDaysKtrfGameplayBridge::LoadSave(
    const std::vector<uint8_t>& bytes, StepResult* out,
    Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr || pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days save load requires an idle open bridge/result");
        return false;
    }
    if (factory.create == nullptr || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days save load factory is incomplete");
        return false;
    }

    *out = {};
    SchoolDaysKtrfSession::LoadPlan plan;
    if (!session.PlanLoadSave(bytes, &plan, error)) return false;

    Gameplay* next = factory.create(plan.scene.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days save load failed to create target Gameplay");
        return false;
    }
    if (!session.CommitLoadSave(plan, error)) {
        factory.destroy(next, factory.userdata);
        return false;
    }

    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = plan.scene;
    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);

    out->kind = StepKind::SwappedScene;
    out->scene = plan.scene;
    return true;
}

bool SchoolDaysKtrfGameplayBridge::LoadRouteMapSelector(
    int selector, StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr || pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days RouteMap load requires an idle open bridge/result");
        return false;
    }
    if (factory.create == nullptr || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days RouteMap load factory is incomplete");
        return false;
    }

    *out = {};
    SchoolDaysKtrfSession::RouteMapCheckpointPlan plan;
    if (!session.PlanRouteMapCheckpoint(selector, &plan, error)) return false;

    Gameplay* next = factory.create(plan.scene.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days RouteMap load failed to create target Gameplay");
        return false;
    }
    if (!session.CommitRouteMapCheckpoint(plan, error)) {
        factory.destroy(next, factory.userdata);
        return false;
    }

    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = plan.scene;
    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);

    out->kind = StepKind::SwappedScene;
    out->scene = plan.scene;
    return true;
}

bool SchoolDaysKtrfGameplayBridge::ResetNewGame(
    StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr || pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days new game requires an idle open bridge/result");
        return false;
    }
    if (factory.create == nullptr || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days new game factory is incomplete");
        return false;
    }

    *out = {};
    SchoolDaysStateStore::Snapshot stateBefore;
    if (!session.State().Capture(&stateBefore, error)) return false;
    const SchoolDaysHistory historyBefore = session.History();
    const SchoolDaysPersistence persistenceBefore = session.Persistence();

    SchoolDaysKtrfScene first;
    if (!session.ResetNewGame(&first, error)) {
        Kotonoha_KtrfError ignored{};
        (void)session.State().Restore(stateBefore, &ignored);
        session.History() = historyBefore;
        session.Persistence() = persistenceBefore;
        return false;
    }

    Gameplay* next = factory.create(first.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        Kotonoha_KtrfError ignored{};
        (void)session.State().Restore(stateBefore, &ignored);
        session.History() = historyBefore;
        session.Persistence() = persistenceBefore;
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days new game failed to create initial Gameplay");
        return false;
    }

    SchoolDaysCoordinate coordinate;
    if (!BuildCoordinate(first, &coordinate, error) ||
        !session.History().Rebase(coordinate, session.State(), error) ||
        !session.Persistence().ReplaceCurrentScriptSnapshot(
            first.sceneKey, UINT32_C(0x3f800000), session.State(), error)) {
        Kotonoha_KtrfError ignored{};
        (void)session.State().Restore(stateBefore, &ignored);
        session.History() = historyBefore;
        session.Persistence() = persistenceBefore;
        factory.destroy(next, factory.userdata);
        return false;
    }

    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = first;
    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);

    out->kind = StepKind::SwappedScene;
    out->scene = first;
    return true;
}

bool SchoolDaysKtrfGameplayBridge::ReleaseCurrentGameplayForFrontend(
    Kotonoha_KtrfError* error) {
    if (!opened || pendingHandoff || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days frontend release requires an idle open bridge");
        return false;
    }
    if (currentGameplay != nullptr) {
        factory.destroy(currentGameplay, factory.userdata);
        currentGameplay = nullptr;
    }
    // Script.Free (EXE 0x4236F0) tears down the script manager after the
    // physical media/gameplay objects.  Its 0x432870 call clears transient BLog
    // plus SLog tag1/tag3/tag4 when RouteLoad==0.  Kotonoha's visible BackLog is
    // reset by Main; clear the semantic route history and structural SLog here.
    session.Persistence().ResetScriptManagerForFrontendReturn();
    session.History().Clear();
    currentScene = {};
    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    return true;
}

bool SchoolDaysKtrfGameplayBridge::DebugJumpToNode(
    uint32_t nodeIndex, StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days debug gameplay bridge is not open or result is null");
        return false;
    }
    if (factory.create == nullptr || factory.destroy == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days debug gameplay factory is incomplete");
        return false;
    }

    SchoolDaysKtrfScene target;
    if (!session.ResolveSceneNode(nodeIndex, &target, error)) return false;

    int targetRoute = 0;
    int targetScene = 0;
    if (!SchoolDaysBackRules::CoordinateForSceneKey(
            target.sceneKey, &targetRoute, &targetScene)) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days debug scene has no authenticated ROUTE/SCENE coordinate");
        return false;
    }

    SchoolDaysStateStore::Snapshot stateBefore;
    if (!session.State().Capture(&stateBefore, error)) return false;
    const SchoolDaysHistory historyBefore = session.History();

    // Create first so a bad/missing ORS cannot strand router state away from the
    // currently live Gameplay.
    Gameplay* next = factory.create(target.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days debug jump failed to create target Gameplay");
        return false;
    }

    if (!session.DebugActivateNode(nodeIndex, error) ||
        !session.State().WriteCoordinate(targetRoute, targetScene, error)) {
        Kotonoha_KtrfError restoreError{};
        (void)session.State().Restore(stateBefore, &restoreError);
        session.History() = historyBefore;
        factory.destroy(next, factory.userdata);
        return false;
    }

    SchoolDaysCoordinate coordinate;
    if (!BuildCoordinate(target, &coordinate, error) ||
        !session.History().Rebase(coordinate, session.State(), error)) {
        Kotonoha_KtrfError restoreError{};
        (void)session.State().Restore(stateBefore, &restoreError);
        session.History() = historyBefore;
        factory.destroy(next, factory.userdata);
        return false;
    }

    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = target;
    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);

    *out = {};
    out->kind = StepKind::SwappedScene;
    out->scene = target;
    return true;
}

bool SchoolDaysKtrfGameplayBridge::ContinueHandoff(
    StepResult* out, Kotonoha_KtrfError* error) {
    if (!opened || out == nullptr || !pendingHandoff) {
        SetError(error, KOTONOHA_KTRF_ERROR_ARGUMENT,
                 "School Days gameplay bridge has no pending handoff");
        return false;
    }

    *out = {};
    out->transitionIndex = pendingStep.transitionIndex;
    out->hopCount = pendingStep.hopCount;
    out->scene = pendingStep.scene;
    out->hooks = pendingStep.hooks;
    out->endings = pendingStep.endings;

    if (pendingStep.kind == SchoolDaysKtrfSession::StepKind::Terminal) {
        if (IsCallback2C(pendingStep.hooks)) {
            session.History().Clear();
        }
        out->kind = StepKind::Terminal;
        pendingStep = {};
        pendingHistoryEntry = {};
        pendingHistoryEntryValid = false;
        pendingHandoff = false;
        return true;
    }
    if (pendingStep.kind != SchoolDaysKtrfSession::StepKind::Scene) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days gameplay bridge pending handoff is malformed");
        return false;
    }

    if (!pendingHistoryEntryValid) {
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days pending handoff has no history proposal");
        return false;
    }
    Gameplay* next = factory.create(pendingStep.scene.orsPath.c_str(), gameContext,
                                    factory.userdata);
    if (next == nullptr) {
        SetError(error, KOTONOHA_KTRF_ERROR_IO,
                 "School Days gameplay factory failed to create handoff scene");
        return false;
    }
    const SchoolDaysHistory historyBefore = session.History();
    const bool historyCommitted = IsCallback2C(pendingStep.hooks)
        ? session.History().Rebase(pendingHistoryEntry.destination,
                                   session.State(), error)
        : session.History().CommitForward(pendingHistoryEntry);
    if (!historyCommitted) {
        session.History() = historyBefore;
        factory.destroy(next, factory.userdata);
        SetError(error, KOTONOHA_KTRF_ERROR_FORMAT,
                 "School Days pending history update failed before handoff");
        return false;
    }
    Gameplay* previous = currentGameplay;
    currentGameplay = next;
    currentScene = pendingStep.scene;
    if (previous != nullptr) factory.destroy(previous, factory.userdata);
    out->kind = StepKind::SwappedScene;
    pendingStep = {};
    pendingHistoryEntry = {};
    pendingHistoryEntryValid = false;
    pendingHandoff = false;
    return true;
}

} // namespace Kotonoha
